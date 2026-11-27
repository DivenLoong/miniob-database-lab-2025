/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/stmt/create_index_stmt.h"

#include "common/lang/unordered_set.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

RC CreateIndexStmt::create(Db *db, const CreateIndexSqlNode &create_index, Stmt *&stmt)
{
  stmt = nullptr;
  if (db == nullptr || create_index.index_name.empty() || create_index.attributes.empty()) {
    return RC::INVALID_ARGUMENT;
  }
  Table *table = db->find_table(create_index.relation_name.c_str());
  if (table == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }
  if (table->find_index(create_index.index_name.c_str()) != nullptr) {
    return RC::SCHEMA_INDEX_NAME_REPEAT;
  }

  vector<const FieldMeta *> fields;
  unordered_set<string> seen;
  for (const string &name : create_index.attributes) {
    const FieldMeta *field = table->table_meta().field(name.c_str());
    if (field == nullptr || !field->visible()) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
    if (!seen.insert(name).second) {
      return RC::INVALID_ARGUMENT;
    }
    fields.push_back(field);
  }

  stmt = new CreateIndexStmt(table, std::move(fields), create_index.index_name, create_index.unique, create_index.full_text);
  return RC::SUCCESS;
}
