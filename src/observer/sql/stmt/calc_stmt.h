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
// Created by Wangyunlai on 2022/6/5.
//

#pragma once

#include "common/sys/rc.h"
#include "sql/expr/expression.h"
#include "sql/parser/expression_binder.h"
#include "sql/stmt/stmt.h"

class Db;
class Table;

/**
 * @brief 描述算术运算语句
 * @ingroup Statement
 */
class CalcStmt : public Stmt
{
public:
  CalcStmt()                   = default;
  virtual ~CalcStmt() override = default;

  StmtType type() const override { return StmtType::CALC; }

public:
  static RC create(CalcSqlNode &calc_sql, Stmt *&stmt)
  {
    auto calc_stmt = make_unique<CalcStmt>();
    BinderContext context;
    ExpressionBinder binder(context);
    for (auto &expression : calc_sql.expressions) {
      RC rc = binder.bind_expression(expression, calc_stmt->expressions_);
      if (OB_FAIL(rc)) {
        return rc;
      }
    }
    stmt = calc_stmt.release();
    return RC::SUCCESS;
  }

public:
  vector<unique_ptr<Expression>> &expressions() { return expressions_; }

private:
  vector<unique_ptr<Expression>> expressions_;
};
