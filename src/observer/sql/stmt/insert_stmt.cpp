/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/stmt/insert_stmt.h"

#include <unordered_set>

#include "storage/db/db.h"
#include "storage/table/table.h"

InsertStmt::InsertStmt(Table *table, vector<Value> values) : table_(table), values_(std::move(values)) {}

RC InsertStmt::create(Db *db, const InsertSqlNode &insert, Stmt *&stmt)
{
  stmt = nullptr;
  if (db == nullptr || insert.values.empty()) {
    return RC::INVALID_ARGUMENT;
  }
  Table *table = db->find_table(insert.relation_name.c_str());
  vector<string> provided = insert.columns;
  const ViewDefinition *view = db->find_view(insert.relation_name);
  if (view != nullptr) {
    if (provided.empty()) {
      for (int i = 0; i < view->schema->table_meta().field_num(); ++i) {
        provided.emplace_back(view->schema->table_meta().field(i)->name());
      }
    }
    const ViewWriteColumn *target = nullptr;
    for (string &column : provided) {
      const auto *source = db->view_write_source(*view, column);
      if (source == nullptr || (target != nullptr &&
          (target->table != source->table || target->qualifier != source->qualifier))) {
        return RC::UNSUPPORTED;
      }
      target = source;
      column = source->name;
    }
    if (target == nullptr) {
      return RC::INVALID_ARGUMENT;
    }
    table = db->find_table(target->table.c_str());
  }
  if (table == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  const auto &metadata = table->table_meta();
  const int offset = metadata.sys_field_num();
  const size_t field_count = metadata.field_num() - offset;
  if (provided.empty()) {
    if (field_count != insert.values.size()) {
      return RC::SCHEMA_FIELD_MISSING;
    }
    stmt = new InsertStmt(table, insert.values);
    return RC::SUCCESS;
  }
  if (provided.size() != insert.values.size()) {
    return RC::SCHEMA_FIELD_MISSING;
  }

  vector<Value> values(field_count);
  for (Value &value : values) {
    value.set_null();
  }
  std::unordered_set<int> seen;
  for (size_t i = 0; i < provided.size(); ++i) {
    const FieldMeta *field = metadata.field(provided[i].c_str());
    if (field == nullptr || !field->visible() || !seen.insert(field->field_id()).second) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
    values[field->field_id()] = insert.values[i];
  }
  stmt = new InsertStmt(table, std::move(values));
  return RC::SUCCESS;
}
