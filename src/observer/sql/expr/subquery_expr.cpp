/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/expr/subquery_expr.h"

#include "session/session.h"
#include "sql/expr/tuple.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/physical_operator.h"
#include "sql/optimizer/logical_plan_generator.h"
#include "sql/optimizer/physical_plan_generator.h"
#include "sql/parser/expression_binder.h"
#include "sql/parser/parse.h"
#include "sql/stmt/calc_stmt.h"
#include "sql/stmt/select_stmt.h"

namespace {
thread_local vector<const Tuple *> outer_tuples;

class OuterTupleScope
{
public:
  explicit OuterTupleScope(const Tuple &tuple) { outer_tuples.push_back(&tuple); }
  ~OuterTupleScope() { outer_tuples.pop_back(); }
};
}

struct SubqueryExpr::State
{
  unique_ptr<PhysicalOperator> plan;
  vector<Value> cache;
  int columns = 0;
  bool correlated = false;
  bool cached = false;
};

unique_ptr<Expression> SubqueryExpr::copy() const
{
  auto result = make_unique<SubqueryExpr>(sql_);
  result->state_ = state_;
  result->additional_conditions_ = additional_conditions_;
  result->row_id_qualifier_ = row_id_qualifier_;
  result->result_type_ = result_type_;
  result->set_name(name());
  return result;
}

RC SubqueryExpr::bind(const BinderContext &outer)
{
  ParsedSqlResult parsed;
  RC rc = parse(sql_.c_str(), &parsed);
  if (OB_FAIL(rc) || parsed.sql_nodes().size() != 1 || outer.db() == nullptr) {
    return RC::SQL_SYNTAX;
  }
  ParsedSqlNode &node = *parsed.sql_nodes().front();
  auto state = std::make_shared<State>();
  Stmt *statement = nullptr;
  if (node.flag == SCF_SELECT) {
    if (!row_id_qualifier_.empty()) {
      node.selection.expressions.clear();
      node.selection.expressions.emplace_back(make_unique<RowIdExpr>(row_id_qualifier_));
    }
    if (!additional_conditions_.empty()) {
      for (const auto &condition : node.selection.conditions) {
        if (condition.disjunctive) {
          return RC::UNSUPPORTED;
        }
      }
      node.selection.conditions.insert(
          node.selection.conditions.end(), additional_conditions_.begin(), additional_conditions_.end());
    }
    rc = SelectStmt::create(outer.db(), node.selection, statement, &outer, &state->correlated);
  } else if (node.flag == SCF_CALC) {
    rc = CalcStmt::create(node.calc, statement);
  } else {
    return RC::UNSUPPORTED;
  }
  if (OB_FAIL(rc)) {
    return rc;
  }
  unique_ptr<Stmt> owner(statement);
  const auto &columns = node.flag == SCF_SELECT ?
      static_cast<SelectStmt *>(statement)->query_expressions() :
      static_cast<CalcStmt *>(statement)->expressions();
  state->columns = columns.size();
  if (state->columns == 1) {
    result_type_ = columns.front()->value_type();
  }

  LogicalPlanGenerator logical;
  unique_ptr<LogicalOperator> plan;
  rc = logical.create(statement, plan);
  if (OB_FAIL(rc)) {
    return rc;
  }
  PhysicalPlanGenerator physical;
  rc = physical.create(*plan, state->plan, Session::current_session());
  if (OB_SUCC(rc)) {
    state_ = std::move(state);
  }
  return rc;
}

RC SubqueryExpr::evaluate(const Tuple &outer, size_t max_rows, bool check_column, vector<Value> &rows) const
{
  if (!state_ || (check_column && state_->columns != 1)) {
    return RC::SCHEMA_FIELD_MISSING;
  }
  if (state_->cached) {
    rows = state_->cache;
    return RC::SUCCESS;
  }

  Session *session = Session::current_session();
  if (session == nullptr) {
    return RC::INTERNAL;
  }
  OuterTupleScope scope(outer);
  PhysicalOperator &plan = *state_->plan;
  RC rc = plan.open(session->current_trx());
  if (OB_SUCC(rc)) {
    while ((max_rows == 0 || rows.size() < max_rows) && (rc = plan.next()) == RC::SUCCESS) {
      Value value;
      if (check_column) {
        rc = plan.current_tuple()->cell_at(0, value);
        if (OB_FAIL(rc)) {
          break;
        }
      }
      rows.emplace_back(std::move(value));
    }
  }
  RC closed = plan.close();
  if (rc != RC::SUCCESS && rc != RC::RECORD_EOF) {
    return rc;
  }
  if (OB_FAIL(closed)) {
    return closed;
  }
  if (!state_->correlated) {
    state_->cache = rows;
    state_->cached = true;
  }
  return RC::SUCCESS;
}

RC SubqueryExpr::get_value(const Tuple &tuple, Value &value) const
{
  vector<Value> rows;
  RC rc = evaluate(tuple, 2, true, rows);
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (rows.empty()) {
    value.set_null();
  } else if (rows.size() == 1) {
    value = std::move(rows.front());
  } else {
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

RC RowIdExpr::get_value(const Tuple &tuple, Value &value) const
{
  RID rid;
  const RC rc = tuple.find_record_id(qualifier_.c_str(), rid);
  if (OB_SUCC(rc)) {
    const string id = rid.to_string();
    value.set_string(id.c_str(), id.size());
  }
  return rc;
}

unique_ptr<Expression> ValueListExpr::copy() const
{
  vector<unique_ptr<Expression>> copied;
  for (const auto &item : values_) {
    copied.emplace_back(item->copy());
  }
  return make_unique<ValueListExpr>(std::move(copied));
}

unique_ptr<Expression> OuterFieldExpr::copy() const
{
  auto result = make_unique<OuterFieldExpr>(field().table(), field().meta(), depth_);
  result->set_qualifier(table_name());
  result->set_name(name());
  return result;
}

RC OuterFieldExpr::get_value(const Tuple &, Value &value) const
{
  const size_t depth = depth_;
  if (depth > outer_tuples.size()) {
    return RC::INTERNAL;
  }
  return FieldExpr::get_value(*outer_tuples[outer_tuples.size() - depth], value);
}
