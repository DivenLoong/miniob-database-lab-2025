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

class ScalarFunctionExpr final : public Expression
{
public:
  ScalarFunctionExpr(string function, vector<unique_ptr<Expression>> arguments)
      : function_(std::move(function)), arguments_(std::move(arguments)) {}

  static bool supports(const char *name);
  const char *function_name() const { return function_.c_str(); }
  ExprType type() const override { return ExprType::SCALAR_FUNCTION; }
  AttrType value_type() const override;
  RC get_value(const Tuple &tuple, Value &value) const override;
  RC try_get_value(Value &value) const override;
  unique_ptr<Expression> copy() const override;
  vector<unique_ptr<Expression>> &arguments() { return arguments_; }
  RC validate_arguments() const;

private:
  RC evaluate(const vector<Value> &args, Value &result) const;
  string function_;
  vector<unique_ptr<Expression>> arguments_;
};
