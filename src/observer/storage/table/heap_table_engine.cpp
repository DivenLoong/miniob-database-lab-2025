/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/table/heap_table_engine.h"

#include <algorithm>
#include <unistd.h>
#include "storage/record/heap_record_scanner.h"
#include "common/log/log.h"
#include "storage/index/bplus_tree_index.h"
#include "storage/index/full_text_index.h"
#include "storage/common/meta_util.h"
#include "storage/db/db.h"


HeapTableEngine::~HeapTableEngine()
{
  if (record_handler_ != nullptr) {
    delete record_handler_;
    record_handler_ = nullptr;
  }

  if (data_buffer_pool_ != nullptr) {
    data_buffer_pool_->close_file();
    data_buffer_pool_ = nullptr;
  }

  for (vector<Index *>::iterator it = indexes_.begin(); it != indexes_.end(); ++it) {
    Index *index = *it;
    delete index;
  }
  indexes_.clear();

  LOG_INFO("Table has been closed: %s", table_meta_->name());
}
RC HeapTableEngine::insert_record(Record &record)
{
  RC rc = RC::SUCCESS;
  rc    = record_handler_->insert_record(record.data(), table_meta_->record_size(), &record.rid());
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Insert record failed. table name=%s, rc=%s", table_meta_->name(), strrc(rc));
    return rc;
  }

  rc = insert_entry_of_indexes(record.data(), record.rid());
  if (rc != RC::SUCCESS) {  // 可能出现了键值重复
    RC rc2 = record_handler_->delete_record(&record.rid());
    if (rc2 != RC::SUCCESS) {
      LOG_PANIC("Failed to rollback record data when insert index entries failed. table name=%s, rc=%d:%s",
                table_meta_->name(), rc2, strrc(rc2));
    }
  }
  return rc;
}

RC HeapTableEngine::insert_chunk(const Chunk& chunk)
{
  RC rc = RC::SUCCESS;
  rc    = record_handler_->insert_chunk(chunk, table_meta_->record_size());
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Insert chunk failed. table name=%s, rc=%s", table_meta_->name(), strrc(rc));
    return rc;
  }

  // TODO: insert chunk support update index
  return rc;
}

RC HeapTableEngine::visit_record(const RID &rid, function<bool(Record &)> visitor)
{
  return record_handler_->visit_record(rid, visitor);
}

RC HeapTableEngine::get_record(const RID &rid, Record &record)
{
  RC rc = record_handler_->get_record(rid, record);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to visit record. rid=%s, table=%s, rc=%s", rid.to_string().c_str(), table_meta_->name(), strrc(rc));
    return rc;
  }

  return rc;
}

RC HeapTableEngine::update_record(const Record &original, const Record &changed)
{
  const RID &rid = original.rid();
  vector<Index *> changed_indexes;
  vector<Index *> changed_full_text;
  RC rc = RC::SUCCESS;
  for (Index *index : indexes_) {
    if (index->same_key(original.data(), changed.data())) {
      continue;
    }
    rc = index->insert_entry(changed.data(), &rid);
    if (OB_FAIL(rc)) {
      break;
    }
    if (index->index_meta().full_text()) {
      changed_full_text.push_back(index);
    } else {
      changed_indexes.push_back(index);
    }
  }

  size_t removed = 0;
  if (OB_SUCC(rc)) {
    for (; removed < changed_indexes.size(); ++removed) {
      rc = changed_indexes[removed]->delete_entry(original.data(), &rid);
      if (OB_FAIL(rc)) {
        break;
      }
    }
  }
  if (OB_SUCC(rc)) {
    rc = record_handler_->visit_record(rid, [&](Record &record) {
      memcpy(record.data(), changed.data(), changed.len());
      return true;
    });
  }
  if (OB_FAIL(rc)) {
    for (Index *index : changed_full_text) {
      RC undo = index->insert_entry(original.data(), &rid);
      if (OB_FAIL(undo)) {
        LOG_ERROR("failed to restore full-text index: %s", strrc(undo));
      }
    }
    for (Index *index : changed_indexes) {
      RC undo = index->delete_entry(changed.data(), &rid);
      if (OB_FAIL(undo)) {
        LOG_ERROR("failed to remove new index key during update rollback: %s", strrc(undo));
      }
    }
    for (size_t i = 0; i < removed; ++i) {
      RC undo = changed_indexes[i]->insert_entry(original.data(), &rid);
      if (OB_FAIL(undo)) {
        LOG_ERROR("failed to restore original index key during update rollback: %s", strrc(undo));
      }
    }
  }
  return rc;
}

RC HeapTableEngine::delete_record(const Record &record)
{
  RC rc = RC::SUCCESS;
  for (Index *index : indexes_) {
    rc = index->delete_entry(record.data(), &record.rid());
    ASSERT(RC::SUCCESS == rc, 
           "failed to delete entry from index. table name=%s, index name=%s, rid=%s, rc=%s",
           table_meta_->name(), index->index_meta().name(), record.rid().to_string().c_str(), strrc(rc));
  }
  rc = record_handler_->delete_record(&record.rid());
  return rc;
}

RC HeapTableEngine::get_record_scanner(RecordScanner *&scanner, Trx *trx, ReadWriteMode mode)
{
  scanner = new HeapRecordScanner(table_, *data_buffer_pool_, trx, db_->log_handler(), mode, nullptr);
  RC rc = scanner->open_scan();
  if (rc != RC::SUCCESS) {
    LOG_ERROR("failed to open scanner. rc=%s", strrc(rc));
  }
  return rc;
}

RC HeapTableEngine::get_chunk_scanner(ChunkFileScanner &scanner, Trx *trx, ReadWriteMode mode)
{
  RC rc = scanner.open_scan_chunk(table_, *data_buffer_pool_, db_->log_handler(), mode);
  if (rc != RC::SUCCESS) {
    LOG_ERROR("failed to open scanner. rc=%s", strrc(rc));
  }
  return rc;
}

RC HeapTableEngine::create_index(Trx *trx, const vector<const FieldMeta *> &fields, const char *index_name, bool unique, bool full_text)
{
  if (fields.empty() || fields.front() == nullptr || find_index(index_name) != nullptr) {
    return RC::INVALID_ARGUMENT;
  }
  IndexMeta new_index_meta;
  RC rc = new_index_meta.init(index_name, fields, unique);
  if (OB_FAIL(rc)) {
    return rc;
  }

  if (full_text && (fields.size() != 1 || unique || table_meta_->sys_field_num() != 0)) {
    return RC::UNSUPPORTED;
  }
  new_index_meta.set_full_text(full_text);
  unique_ptr<Index> index;
  if (full_text) {
    index = make_unique<FullTextIndex>();
  } else {
    index = make_unique<BplusTreeIndex>();
  }
  const string index_file = table_index_file(db_->path().c_str(), table_meta_->name(), index_name);
  rc = index->create(table_, index_file.c_str(), new_index_meta, *fields.front());
  if (OB_FAIL(rc)) {
    return rc;
  }

  RecordScanner *raw_scanner = nullptr;
  rc = get_record_scanner(raw_scanner, trx, ReadWriteMode::READ_ONLY);
  unique_ptr<RecordScanner> scanner(raw_scanner);
  if (OB_SUCC(rc)) {
    Record record;
    while ((rc = scanner->next(record)) == RC::SUCCESS) {
      rc = index->insert_entry(record.data(), &record.rid());
      if (OB_FAIL(rc)) {
        break;
      }
    }
    scanner->close_scan();
    if (rc == RC::RECORD_EOF) {
      rc = index->sync();
    }
  }

  if (OB_SUCC(rc)) {
    TableMeta changed(*table_meta_);
    changed.add_index(new_index_meta);
    rc = persist_metadata(changed);
  }
  if (OB_FAIL(rc)) {
    index.reset();
    unlink(index_file.c_str());
    return rc;
  }

  indexes_.push_back(index.release());
  return RC::SUCCESS;
}

RC HeapTableEngine::drop_index(const char *name)
{
  const auto found = std::find_if(indexes_.begin(), indexes_.end(), [&](Index *index) {
    return strcmp(index->index_meta().name(), name) == 0;
  });
  if (found == indexes_.end()) {
    return RC::NOTFOUND;
  }
  RC rc = db_->sync();
  if (OB_FAIL(rc)) {
    return rc;
  }
  const string file = table_index_file(db_->path().c_str(), table_meta_->name(), name);
  const string detached = file + ".dropping";
  if (access(detached.c_str(), F_OK) == 0 || rename(file.c_str(), detached.c_str()) != 0) {
    return RC::IOERR_WRITE;
  }
  TableMeta changed(*table_meta_);
  changed.remove_index(name);
  rc = persist_metadata(changed);
  if (OB_FAIL(rc)) {
    if (rename(detached.c_str(), file.c_str()) != 0) {
      LOG_ERROR("failed to restore index file %s", file.c_str());
    }
    return rc;
  }
  delete *found;
  indexes_.erase(found);
  if (unlink(detached.c_str()) != 0) {
    LOG_ERROR("failed to remove detached index file %s", detached.c_str());
  }
  return RC::SUCCESS;
}

RC HeapTableEngine::persist_metadata(TableMeta &metadata)
{
  const string path = table_meta_file(db_->path().c_str(), table_meta_->name());
  const string temporary = path + ".tmp";
  fstream output(temporary, ios_base::out | ios_base::binary | ios_base::trunc);
  if (!output.is_open()) {
    return RC::IOERR_OPEN;
  }
  if (metadata.serialize(output) < 0 || !output) {
    output.close();
    unlink(temporary.c_str());
    return RC::IOERR_WRITE;
  }
  output.close();
  if (!output || rename(temporary.c_str(), path.c_str()) != 0) {
    unlink(temporary.c_str());
    return RC::IOERR_WRITE;
  }
  table_meta_->swap(metadata);
  return RC::SUCCESS;
}

RC HeapTableEngine::insert_entry_of_indexes(const char *record, const RID &rid)
{
  for (size_t i = 0; i < indexes_.size(); ++i) {
    RC rc = indexes_[i]->insert_entry(record, &rid);
    if (OB_FAIL(rc)) {
      while (i > 0) {
        RC undo = indexes_[--i]->delete_entry(record, &rid);
        if (OB_FAIL(undo)) {
          LOG_ERROR("failed to reverse index insertion: %s", strrc(undo));
        }
      }
      return rc;
    }
  }
  return RC::SUCCESS;
}

RC HeapTableEngine::sync()
{
  RC rc = RC::SUCCESS;
  for (Index *index : indexes_) {
    rc = index->sync();
    if (rc != RC::SUCCESS) {
      LOG_ERROR("Failed to flush index's pages. table=%s, index=%s, rc=%d:%s",
          table_meta_->name(),
          index->index_meta().name(),
          rc,
          strrc(rc));
      return rc;
    }
  }

  rc = data_buffer_pool_->flush_all_pages();
  LOG_INFO("Sync table over. table=%s", table_meta_->name());
  return rc;
}

Index *HeapTableEngine::find_index(const char *index_name) const
{
  for (Index *index : indexes_) {
    if (0 == strcmp(index->index_meta().name(), index_name)) {
      return index;
    }
  }
  return nullptr;
}
Index *HeapTableEngine::find_index_by_field(const char *field_name) const
{
  const IndexMeta *index_meta = table_meta_->find_index_by_field(field_name);
  if (index_meta != nullptr) {
    return this->find_index(index_meta->name());
  }
  return nullptr;
}

RC HeapTableEngine::init()
{
  string data_file = table_data_file(db_->path().c_str(), table_meta_->name());

  BufferPoolManager &bpm = db_->buffer_pool_manager();
  RC                 rc  = bpm.open_file(db_->log_handler(), data_file.c_str(), data_buffer_pool_);
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to open disk buffer pool for file:%s. rc=%d:%s", data_file.c_str(), rc, strrc(rc));
    return rc;
  }

  record_handler_ = new RecordFileHandler(table_meta_->storage_format());

  rc = record_handler_->init(*data_buffer_pool_, db_->log_handler(), table_meta_, table_->lob_handler_);
  if (rc != RC::SUCCESS) {
    LOG_ERROR("Failed to init record handler. rc=%s", strrc(rc));
    delete record_handler_;
    record_handler_ = nullptr;
    return rc;
  }

  return rc;
}

RC HeapTableEngine::open()
{
  RC rc = init();
  if (OB_FAIL(rc)) {
    return rc;
  }
  const int index_num = table_meta_->index_num();
  for (int i = 0; i < index_num; i++) {
    const IndexMeta *index_meta = table_meta_->index(i);
    const FieldMeta *field_meta = table_meta_->field(index_meta->field());
    if (field_meta == nullptr) {
      LOG_ERROR("Found invalid index meta info which has a non-exists field. table=%s, index=%s, field=%s",
                table_meta_->name(), index_meta->name(), index_meta->field());
      // skip cleanup
      //  do all cleanup action in destructive Table function
      return RC::INTERNAL;
    }

    Index *index = index_meta->full_text() ? static_cast<Index *>(new FullTextIndex) : new BplusTreeIndex;
    string          index_file = table_index_file(db_->path().c_str(), table_meta_->name(), index_meta->name());

    rc = index->open(table_, index_file.c_str(), *index_meta, *field_meta);
    if (rc != RC::SUCCESS) {
      delete index;
      LOG_ERROR("Failed to open index. table=%s, index=%s, file=%s, rc=%s",
                table_meta_->name(), index_meta->name(), index_file.c_str(), strrc(rc));
      // skip cleanup
      //  do all cleanup action in destructive Table function.
      return rc;
    }
    if (index_meta->full_text()) {
      RecordScanner *raw = nullptr;
      rc = get_record_scanner(raw, nullptr, ReadWriteMode::READ_ONLY);
      unique_ptr<RecordScanner> scanner(raw);
      Record row;
      while (OB_SUCC(rc) && (rc = scanner->next(row)) == RC::SUCCESS) {
        rc = index->insert_entry(row.data(), &row.rid());
      }
      if (scanner) {
        scanner->close_scan();
      }
      if (rc != RC::RECORD_EOF) {
        delete index;
        return rc;
      }
      rc = RC::SUCCESS;
    }
    indexes_.push_back(index);
  }
  return rc;
}
