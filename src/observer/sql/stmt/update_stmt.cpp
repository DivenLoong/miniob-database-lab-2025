/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/stmt/update_stmt.h"

#include <unordered_set>

#include "storage/db/db.h"
#include "storage/table/table.h"

RC UpdateStmt::create(Db *db, const UpdateSqlNode &update, Stmt *&stmt)
{
  stmt = nullptr;
  if (db == nullptr) {
    return RC::INVALID_ARGUMENT;
  }
  Table *table = db->find_table(update.relation_name.c_str());
  const ViewDefinition *view = db->find_view(update.relation_name);
  vector<ConditionSqlNode> conditions;
  if (view != nullptr) {
    RC rc = db->view_write_conditions(*view, update.conditions, conditions);
    if (OB_FAIL(rc)) {
      return rc;
    }
    table = db->find_table(view->write_table.c_str());
  } else {
    conditions = update.conditions;
  }
  if (table == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  vector<UpdateColumnValue> values;
  std::unordered_set<int> selected;
  for (const auto &assignment : update.assignments) {
    const string name = view == nullptr ? assignment.name : db->view_write_column(*view, assignment.name);
    const FieldMeta *field = table->table_meta().field(name.c_str());
    if (field == nullptr || !field->visible() || !selected.insert(field->field_id()).second) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
    Value value = assignment.value;
    if (value.is_null() && !field->nullable()) {
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    if (!value.is_null() && value.attr_type() != field->type() &&
        !(field->type() == AttrType::TEXTS && value.attr_type() == AttrType::CHARS)) {
      RC rc = Value::cast_to(assignment.value, field->type(), value);
      if (OB_FAIL(rc)) {
        return rc;
      }
    }
    values.push_back({field, std::move(value)});
  }

  unordered_map<string, Table *> tables{{table->name(), table}};
  FilterStmt *filter = nullptr;
  RC rc = FilterStmt::create(db, table, &tables, conditions.data(), conditions.size(), filter);
  if (OB_FAIL(rc)) {
    return rc;
  }
  stmt = new UpdateStmt(table, std::move(values), unique_ptr<FilterStmt>(filter));
  return RC::SUCCESS;
}
