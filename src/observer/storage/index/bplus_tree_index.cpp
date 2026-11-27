/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by wangyunlai.wyl on 2021/5/19.
//

#include "storage/index/bplus_tree_index.h"

#include <algorithm>
#include <cmath>
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/db/db.h"

BplusTreeIndex::~BplusTreeIndex() noexcept { close(); }

RC BplusTreeIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    LOG_WARN("Failed to create index due to the index has been created before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.field());
    return RC::RECORD_OPENNED;
  }

  Index::init(index_meta, field_meta);

  RC rc = init_key(table, index_meta);
  if (OB_FAIL(rc)) {
    return rc;
  }
  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  const AttrType key_type = key_fields_.size() == 1 ? field_meta.type() : AttrType::INDEX_KEYS;
  const int key_len = key_fields_.size() == 1 ? field_meta.len() : packed_size_;
  rc = index_handler_.create(table->db()->log_handler(), bpm, file_name, key_type, key_len);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to create index_handler, file_name:%s, index:%s, field:%s, rc:%s",
        file_name, index_meta.name(), index_meta.field(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully create index, file_name:%s, index:%s, field:%s",
    file_name, index_meta.name(), index_meta.field());
  return RC::SUCCESS;
}

RC BplusTreeIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    LOG_WARN("Failed to open index due to the index has been initedd before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.field());
    return RC::RECORD_OPENNED;
  }

  Index::init(index_meta, field_meta);

  RC rc = init_key(table, index_meta);
  if (OB_FAIL(rc)) {
    return rc;
  }
  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  rc = index_handler_.open(table->db()->log_handler(), bpm, file_name);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to open index_handler, file_name:%s, index:%s, field:%s, rc:%s",
        file_name, index_meta.name(), index_meta.field(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully open index, file_name:%s, index:%s, field:%s",
    file_name, index_meta.name(), index_meta.field());
  return RC::SUCCESS;
}

RC BplusTreeIndex::close()
{
  if (inited_) {
    LOG_INFO("Begin to close index, index:%s, field:%s", index_meta_.name(), index_meta_.field());
    index_handler_.close();
    inited_ = false;
  }
  LOG_INFO("Successfully close index.");
  return RC::SUCCESS;
}

RC BplusTreeIndex::init_key(Table *table, const IndexMeta &meta)
{
  key_fields_.clear();
  packed_size_ = 0;
  for (const auto &name : meta.fields()) {
    const FieldMeta *field = table->table_meta().field(name.c_str());
    if (field == nullptr) {
      return RC::SCHEMA_FIELD_MISSING;
    }
    if (field->type() != AttrType::INTS && field->type() != AttrType::FLOATS &&
        field->type() != AttrType::CHARS && field->type() != AttrType::DATES) {
      return RC::UNSUPPORTED;
    }
    if (field->len() > 1023 - packed_size_) {
      return RC::INVALID_ARGUMENT;
    }
    key_fields_.push_back(*field);
    packed_size_ += 1 + field->len();
  }
  return RC::SUCCESS;
}

void BplusTreeIndex::encode_part(char *destination, const FieldMeta &field, const char *source, size_t size) const
{
  if (field.type() == AttrType::CHARS) {
    memset(destination, 0, field.len());
    memcpy(destination, source, std::min(size, static_cast<size_t>(field.len())));
    return;
  }

  uint32_t bits = 0;
  memcpy(&bits, source, sizeof(bits));
  constexpr uint32_t sign = 0x80000000u;
  if (field.type() == AttrType::FLOATS) {
    if ((bits & ~sign) == 0) {
      bits = 0;
    }
    bits = (bits & sign) != 0 ? ~bits : bits ^ sign;
  } else {
    bits ^= sign;
  }
  for (int i = 0; i < 4; ++i) {
    destination[i] = static_cast<char>(bits >> (24 - i * 8));
  }
}

string BplusTreeIndex::pack_key(const char *record, bool &has_null) const
{
  string key(packed_size_, '\0');
  int offset = 0;
  has_null = false;
  for (const FieldMeta &field : key_fields_) {
    if (field.is_null(record)) {
      has_null = true;
    } else {
      key[offset] = 1;
      encode_part(key.data() + offset + 1, field, record + field.offset(), field.len());
    }
    offset += 1 + field.len();
  }
  return key;
}

string BplusTreeIndex::scan_bound(const char *key, int len, unsigned char suffix) const
{
  string bound(packed_size_, static_cast<char>(suffix));
  bound[0] = 1;
  encode_part(bound.data() + 1, key_fields_.front(), key, len);
  return bound;
}

bool BplusTreeIndex::same_key(const char *left, const char *right) const
{
  if (key_fields_.size() == 1) {
    return Index::same_key(left, right);
  }
  bool has_null = false;
  return pack_key(left, has_null) == pack_key(right, has_null);
}

RC BplusTreeIndex::insert_entry(const char *record, const RID *rid)
{
  const bool composite = key_fields_.size() > 1;
  bool has_null = field_meta_.is_null(record);
  string key;
  if (composite) {
    key = pack_key(record, has_null);
  } else if (has_null) {
    return RC::SUCCESS;
  }
  const char *data = composite ? key.data() : record + field_meta_.offset();
  const int length = composite ? packed_size_ : field_meta_.len();
  std::unique_lock<std::mutex> guard(uniqueness_mutex_, std::defer_lock);
  if (index_meta_.unique() && !has_null) {
    guard.lock();
    list<RID> existing;
    RC rc = index_handler_.get_entry(data, length, existing);
    if (OB_FAIL(rc)) {
      return rc;
    }
    if (!existing.empty()) {
      return RC::RECORD_DUPLICATE_KEY;
    }
  }
  return index_handler_.insert_entry(data, rid);
}

RC BplusTreeIndex::delete_entry(const char *record, const RID *rid)
{
  if (key_fields_.size() > 1) {
    bool has_null;
    const string key = pack_key(record, has_null);
    return index_handler_.delete_entry(key.data(), rid);
  }
  if (field_meta_.is_null(record)) {
    return RC::SUCCESS;
  }
  return index_handler_.delete_entry(record + field_meta_.offset(), rid);
}

IndexScanner *BplusTreeIndex::create_scanner(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  string left_bound;
  string right_bound;
  if (key_fields_.size() > 1) {
    if (left_key != nullptr) {
      left_bound = scan_bound(left_key, left_len, left_inclusive ? 0 : 255);
      left_key = left_bound.data();
      left_len = packed_size_;
    }
    if (right_key != nullptr) {
      right_bound = scan_bound(right_key, right_len, right_inclusive ? 255 : 0);
      right_key = right_bound.data();
      right_len = packed_size_;
    }
  }
  BplusTreeIndexScanner *index_scanner = new BplusTreeIndexScanner(index_handler_);
  RC rc = index_scanner->open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open index scanner. rc=%d:%s", rc, strrc(rc));
    delete index_scanner;
    return nullptr;
  }
  return index_scanner;
}

RC BplusTreeIndex::sync() { return index_handler_.sync(); }

////////////////////////////////////////////////////////////////////////////////
BplusTreeIndexScanner::BplusTreeIndexScanner(BplusTreeHandler &tree_handler) : tree_scanner_(tree_handler) {}

BplusTreeIndexScanner::~BplusTreeIndexScanner() noexcept { tree_scanner_.close(); }

RC BplusTreeIndexScanner::open(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  return tree_scanner_.open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
}

RC BplusTreeIndexScanner::next_entry(RID *rid) { return tree_scanner_.next_entry(*rid); }

RC BplusTreeIndexScanner::destroy()
{
  delete this;
  return RC::SUCCESS;
}
