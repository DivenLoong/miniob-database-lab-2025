/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/stmt/filter_stmt.h"

#include "sql/parser/expression_binder.h"
#include "sql/expr/expression_iterator.h"

RC FilterStmt::create(Db *, Table *default_table, unordered_map<string, Table *> *tables,
    const ConditionSqlNode *conditions, int condition_num, FilterStmt *&stmt, bool allow_aggregates)
{
  stmt = nullptr;
  BinderContext context;
  if (tables != nullptr) {
    for (const auto &[name, table] : *tables) {
      context.add_table(table);
    }
  } else if (default_table != nullptr) {
    context.add_table(default_table);
  }
  ExpressionBinder binder(context);
  auto result = make_unique<FilterStmt>();
  for (int i = 0; i < condition_num; ++i) {
    const ConditionSqlNode &condition = conditions[i];
    auto comparison = make_unique<ComparisonExpr>(
        condition.comp, condition.left_expression->copy(), condition.right_expression->copy());
    unique_ptr<Expression> expression = std::move(comparison);
    RC rc = binder.bind_expression(expression, result->predicates_);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  if (!allow_aggregates) {
    std::function<RC(unique_ptr<Expression> &)> validate = [&](unique_ptr<Expression> &expression) {
      if (expression->type() == ExprType::AGGREGATION) {
        return RC::INVALID_ARGUMENT;
      }
      return ExpressionIterator::iterate_child_expr(*expression, validate);
    };
    for (auto &predicate : result->predicates_) {
      RC rc = validate(predicate);
      if (OB_FAIL(rc)) {
        return rc;
      }
    }
  }
  stmt = result.release();
  return RC::SUCCESS;
}
