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
#include "sql/stmt/stmt.h"
#include "storage/field/field.h"

class FieldMeta;
class FilterStmt;
class Db;
class BinderContext;
class Table;

/**
 * @brief 表示select语句
 * @ingroup Statement
 */
class SelectStmt : public Stmt
{
public:
  SelectStmt() = default;
  ~SelectStmt() override;

  StmtType type() const override { return StmtType::SELECT; }

public:
  static RC create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt, const BinderContext *outer = nullptr, bool *correlated = nullptr);

public:
  const vector<Table *> &tables() const { return tables_; }
  const vector<string> &qualifiers() const { return qualifiers_; }
  const vector<unique_ptr<Stmt>> &view_sources() const { return view_sources_; }
  FilterStmt            *filter_stmt() const { return filter_stmt_; }
  FilterStmt            *having_stmt() const { return having_stmt_; }

  vector<unique_ptr<Expression>> &query_expressions() { return query_expressions_; }
  vector<unique_ptr<Expression>> &group_by() { return group_by_; }
  vector<OrderBySqlNode> &order_by() { return order_by_; }
  int limit() const { return limit_; }

private:
  vector<unique_ptr<Expression>> query_expressions_;
  vector<Table *>                tables_;
  vector<string>                 qualifiers_;
  vector<unique_ptr<Stmt>>       view_sources_;
  FilterStmt                    *filter_stmt_ = nullptr;
  FilterStmt                    *having_stmt_ = nullptr;
  vector<unique_ptr<Expression>> group_by_;
  vector<OrderBySqlNode> order_by_;
  int limit_ = -1;
};
