/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/expr/expression.h"
#include "sql/parser/parse_defs.h"

class BinderContext;

class SubqueryExpr final : public Expression
{
public:
  explicit SubqueryExpr(string sql) : sql_(std::move(sql)) {}
  ExprType type() const override { return ExprType::SUBQUERY; }
  AttrType value_type() const override { return result_type_; }
  unique_ptr<Expression> copy() const override;
  RC get_value(const Tuple &tuple, Value &value) const override;
  RC bind(const BinderContext &outer);
  vector<ConditionSqlNode> &additional_conditions() { return additional_conditions_; }
  void project_record_id(const string &qualifier) { row_id_qualifier_ = qualifier; }
  RC evaluate(const Tuple &outer, size_t max_rows, bool check_column, vector<Value> &rows) const;

private:
  struct State;
  string sql_;
  vector<ConditionSqlNode> additional_conditions_;
  string row_id_qualifier_;
  AttrType result_type_ = AttrType::UNDEFINED;
  shared_ptr<State> state_;
};

class RowIdExpr final : public Expression
{
public:
  explicit RowIdExpr(string qualifier) : qualifier_(std::move(qualifier)) { set_name("row_id"); }
  ExprType type() const override { return ExprType::ROW_ID; }
  AttrType value_type() const override { return AttrType::CHARS; }
  RC get_value(const Tuple &tuple, Value &value) const override;
  unique_ptr<Expression> copy() const override { return make_unique<RowIdExpr>(qualifier_); }

private:
  string qualifier_;
};

class ValueListExpr final : public Expression
{
public:
  explicit ValueListExpr(vector<unique_ptr<Expression>> values) : values_(std::move(values)) {}
  ExprType type() const override { return ExprType::VALUE_LIST; }
  AttrType value_type() const override { return AttrType::UNDEFINED; }
  RC get_value(const Tuple &, Value &) const override { return RC::INVALID_ARGUMENT; }
  unique_ptr<Expression> copy() const override;
  vector<unique_ptr<Expression>> &values() { return values_; }
  const vector<unique_ptr<Expression>> &values() const { return values_; }

private:
  vector<unique_ptr<Expression>> values_;
};

class OuterFieldExpr final : public FieldExpr
{
public:
  OuterFieldExpr(const Table *table, const FieldMeta *field, int depth)
      : FieldExpr(table, field), depth_(depth) {}
  ExprType type() const override { return ExprType::OUTER_FIELD; }
  unique_ptr<Expression> copy() const override;
  RC get_value(const Tuple &tuple, Value &value) const override;

private:
  int depth_;
};
