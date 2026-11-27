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

#include "sql/expr/expression_iterator.h"
#include "sql/parser/expression_binder.h"
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
  string qualifier;
  if (view != nullptr) {
    const ViewWriteColumn *target = nullptr;
    for (const auto &assignment : update.assignments) {
      const auto *source = db->view_write_source(*view, assignment.name);
      if (source == nullptr || (target != nullptr &&
          (source->table != target->table || source->qualifier != target->qualifier))) {
        return RC::UNSUPPORTED;
      }
      target = source;
    }
    if (target == nullptr) {
      return RC::INVALID_ARGUMENT;
    }
    table = db->find_table(target->table.c_str());
    RC rc;
    if (view->write_table.empty()) {
      qualifier = "__miniob_write_target";
      rc = db->joined_view_write_condition(*view, *target, qualifier, update.conditions, conditions);
    } else {
      rc = db->view_write_conditions(*view, update.conditions, conditions);
    }
    if (OB_FAIL(rc)) {
      return rc;
    }
  } else {
    conditions = update.conditions;
  }
  if (table == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  BinderContext context(db);
  context.add_table(table, qualifier);
  ExpressionBinder binder(context);
  vector<UpdateColumnValue> values;
  std::unordered_set<int> selected;
  for (const auto &assignment : update.assignments) {
    const string name = view == nullptr ? assignment.name : db->view_write_column(*view, assignment.name);
    const FieldMeta *field = table->table_meta().field(name.c_str());
    if (field == nullptr || !field->visible() || !selected.insert(field->field_id()).second) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
    auto expression = assignment.expression->copy();
    vector<unique_ptr<Expression>> bound;
    RC rc = binder.bind_expression(expression, bound);
    if (OB_FAIL(rc)) {
      return rc;
    }
    if (bound.size() != 1 || bound.front()->type() == ExprType::AGGREGATION) {
      return RC::INVALID_ARGUMENT;
    }
    if (!field->nullable() && bound.front()->value_type() == AttrType::NULLS) {
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    values.push_back({field, std::move(bound.front())});
  }

  FilterStmt *filter = nullptr;
  RC rc = FilterStmt::create(db, table, nullptr, conditions.data(), conditions.size(), filter, false, &context);
  if (OB_FAIL(rc)) {
    return rc;
  }
  bool reads_subquery = false;
  function<void(Expression &)> inspect = [&](Expression &expression) {
    if (expression.type() == ExprType::SUBQUERY) {
      reads_subquery = true;
      return;
    }
    ExpressionIterator::iterate_child_expr(expression, [&](unique_ptr<Expression> &child) {
      inspect(*child);
      return RC::SUCCESS;
    });
  };
  for (const auto &column : values) {
    inspect(*column.expression);
  }
  for (const auto &predicate : filter->predicates()) {
    inspect(*predicate);
  }
  stmt = new UpdateStmt(
      table, std::move(qualifier), std::move(values), unique_ptr<FilterStmt>(filter), reads_subquery);
  return RC::SUCCESS;
}
