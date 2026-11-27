/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/db/db.h"

#include <algorithm>
#include <fstream>

#include "common/log/log.h"
#include "storage/common/meta_util.h"
#include "storage/index/index_meta.h"
#include "storage/record/record_scanner.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

namespace {
vector<string> table_files(const string &directory, const string &name, const TableMeta &metadata)
{
  vector<string> files{table_meta_file(directory.c_str(), name.c_str()),
      table_data_file(directory.c_str(), name.c_str())};
  for (int i = 0; i < metadata.index_num(); ++i) {
    files.push_back(table_index_file(directory.c_str(), name.c_str(), metadata.index(i)->name()));
  }
  for (int i = 0; i < metadata.field_num(); ++i) {
    const AttrType type = metadata.field(i)->type();
    if (type == AttrType::TEXTS || type == AttrType::VECTORS) {
      files.push_back(table_lob_file(directory.c_str(), name.c_str()));
      break;
    }
  }
  return files;
}

struct FileMoves
{
  vector<std::pair<string, string>> moves;
  RC move(const string &from, const string &to)
  {
    std::error_code error;
    if (filesystem::exists(to, error) || error) {
      return RC::IOERR_ACCESS;
    }
    filesystem::rename(from, to, error);
    if (error) {
      return RC::IOERR_WRITE;
    }
    moves.emplace_back(from, to);
    return RC::SUCCESS;
  }
  void reverse()
  {
    for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
      std::error_code error;
      filesystem::rename(it->second, it->first, error);
      if (error) {
        LOG_ERROR("failed to recover ALTER file %s: %s", it->first.c_str(), error.message().c_str());
      }
    }
  }
};
}

RC Db::alter_table(const AlterTableSqlNode &alter)
{
  using Operation = AlterTableSqlNode::Operation;
  Table *original = find_table(alter.table_name.c_str());
  if (original == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }
  const TableMeta &metadata = original->table_meta();
  if (metadata.storage_engine() != StorageEngine::HEAP || metadata.storage_format() != StorageFormat::ROW_FORMAT ||
      metadata.sys_field_num() != 0) {
    return RC::UNSUPPORTED;
  }

  string target = alter.table_name;
  if (alter.operation == Operation::RENAME) {
    target = alter.new_table;
    if (target == alter.table_name) {
      return RC::SUCCESS;
    }
    if (find_table(target.c_str()) != nullptr) {
      return RC::SCHEMA_TABLE_EXIST;
    }
  } else if (alter.operation == Operation::ADD || alter.operation == Operation::CHANGE) {
    if (metadata.field(alter.column.name.c_str()) != nullptr && alter.column.name != alter.old_column) {
      return RC::INVALID_ARGUMENT;
    }
  }
  if ((alter.operation == Operation::DROP || alter.operation == Operation::CHANGE) &&
      metadata.field(alter.old_column.c_str()) == nullptr) {
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  vector<AttrInfoSqlNode> columns;
  vector<const FieldMeta *> sources;
  vector<string> primary_keys = metadata.primary_keys();
  for (int i = 0; i < metadata.field_num(); ++i) {
    const FieldMeta *field = metadata.field(i);
    if (alter.operation == Operation::DROP && alter.old_column == field->name()) {
      std::erase(primary_keys, alter.old_column);
      continue;
    }
    const size_t length = field->type() == AttrType::VECTORS ? field->vector_dimension() : field->len();
    AttrInfoSqlNode column{field->type(), field->name(), length, field->nullable(), true};
    if (alter.operation == Operation::CHANGE && alter.old_column == field->name()) {
      if (alter.column.type != field->type() ||
          (field->type() != AttrType::TEXTS && alter.column.length != length)) {
        return RC::UNSUPPORTED;
      }
      column.name = alter.column.name;
      for (auto &key : primary_keys) {
        if (key == alter.old_column) {
          key = column.name;
        }
      }
    }
    columns.push_back(std::move(column));
    sources.push_back(field);
  }
  if (alter.operation == Operation::ADD) {
    auto column = alter.column;
    if (!column.nullable_explicit) {
      column.nullable = true;
    }
    columns.push_back(std::move(column));
    sources.push_back(nullptr);
  }
  if (columns.empty()) {
    return RC::INVALID_ARGUMENT;
  }

  string staging = "__miniob_alter_" + std::to_string(next_table_id_);
  while (find_table(staging.c_str()) != nullptr || staging == target) {
    staging += '_';
  }
  RC rc = create_table(staging.c_str(), columns, primary_keys, metadata.storage_format());
  if (OB_FAIL(rc)) {
    return rc;
  }
  Table *replacement = find_table(staging.c_str());
  const auto discard = [&] {
    const RC removed = drop_table(staging.c_str());
    if (OB_FAIL(removed)) {
      LOG_ERROR("failed to clean staging table %s: %s", staging.c_str(), strrc(removed));
    }
  };

  RecordScanner *raw = nullptr;
  rc = original->get_record_scanner(raw, nullptr, ReadWriteMode::READ_ONLY);
  unique_ptr<RecordScanner> scanner(raw);
  Record record;
  while (OB_SUCC(rc) && (rc = scanner->next(record)) == RC::SUCCESS) {
    vector<Value> values(sources.size());
    for (size_t i = 0; i < sources.size(); ++i) {
      if (sources[i] == nullptr) {
        values[i].set_null();
      } else {
        rc = original->read_value(*sources[i], record.data(), values[i]);
        if (OB_FAIL(rc)) {
          break;
        }
      }
    }
    if (OB_SUCC(rc)) {
      Record row;
      rc = replacement->make_record(values.size(), values.data(), row);
      if (OB_SUCC(rc)) {
        rc = replacement->insert_record(row);
      }
    }
  }
  if (scanner) {
    scanner->close_scan();
    scanner.reset();
  }
  if (rc != RC::RECORD_EOF) {
    discard();
    return rc;
  }

  for (int i = 0; i < metadata.index_num(); ++i) {
    const IndexMeta *index = metadata.index(i);
    vector<const FieldMeta *> fields;
    for (const string &name : index->fields()) {
      const string &updated = alter.operation == Operation::CHANGE && name == alter.old_column ? alter.column.name : name;
      const FieldMeta *field = replacement->table_meta().field(updated.c_str());
      if (field == nullptr) {
        break;
      }
      fields.push_back(field);
    }
    if (fields.size() == index->fields().size()) {
      rc = replacement->create_index(nullptr, fields, index->name(), index->unique(), index->full_text());
      if (OB_FAIL(rc)) {
        discard();
        return rc;
      }
    }
  }

  rc = sync();
  if (OB_FAIL(rc)) {
    discard();
    return rc;
  }
  TableMeta final_meta(replacement->table_meta());
  final_meta.set_name(target);
  const auto old_files = table_files(path_, alter.table_name, metadata);
  const auto staging_files = table_files(path_, staging, replacement->table_meta());
  const auto new_files = table_files(path_, target, final_meta);
  const string final_meta_temp = staging_files.front() + ".rewrite";
  {
    std::ofstream output(final_meta_temp, std::ios::binary | std::ios::trunc);
    if (!output || final_meta.serialize(output) < 0 || !output) {
      output.close();
      std::error_code ignored;
      filesystem::remove(final_meta_temp, ignored);
      discard();
      return RC::IOERR_WRITE;
    }
  }

  const string suffix = ".alter-old-" + std::to_string(original->table_id());
  FileMoves moves;
  for (const auto &file : old_files) {
    rc = moves.move(file, file + suffix);
    if (OB_FAIL(rc)) {
      break;
    }
  }
  for (size_t i = 1; OB_SUCC(rc) && i < staging_files.size(); ++i) {
    rc = moves.move(staging_files[i], new_files[i]);
  }
  if (OB_SUCC(rc)) {
    rc = moves.move(final_meta_temp, new_files.front());
  }
  if (OB_FAIL(rc)) {
    moves.reverse();
    std::error_code ignored;
    filesystem::remove(final_meta_temp, ignored);
    discard();
    return rc;
  }

  // Reopen under the new paths after releasing both sets of original handles.
  opened_tables_.erase(staging);
  opened_tables_.erase(alter.table_name);
  delete replacement;
  delete original;

  auto reopened = make_unique<Table>();
  const string meta_name = filesystem::path(new_files.front()).filename().string();
  rc = reopened->open(this, meta_name.c_str(), path_.c_str());
  if (OB_FAIL(rc)) {
    reopened.reset();
    moves.reverse();
    auto recovered = make_unique<Table>();
    const string old_meta = filesystem::path(old_files.front()).filename().string();
    if (OB_SUCC(recovered->open(this, old_meta.c_str(), path_.c_str()))) {
      opened_tables_[alter.table_name] = recovered.release();
    }
    auto staged = make_unique<Table>();
    const string stage_meta = filesystem::path(staging_files.front()).filename().string();
    if (OB_SUCC(staged->open(this, stage_meta.c_str(), path_.c_str()))) {
      opened_tables_[staging] = staged.release();
      discard();
    }
    return rc;
  }
  opened_tables_[target] = reopened.release();

  std::error_code cleanup;
  filesystem::remove(staging_files.front(), cleanup);
  for (const auto &file : old_files) {
    filesystem::remove(file + suffix, cleanup);
  }
  return RC::SUCCESS;
}
