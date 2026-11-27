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
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/parser/expression_binder.h"

using namespace std;
using namespace common;

SelectStmt::~SelectStmt()
{
  delete having_stmt_;
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
}

RC SelectStmt::create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt, const BinderContext *outer, bool *correlated)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  BinderContext binder_context(db, outer);
  vector<Table *> tables;
  vector<string> qualifiers;
  vector<unique_ptr<Stmt>> view_sources;
  for (const RelationSqlNode &relation : select_sql.relations) {
    Table *table = db->find_table(relation.name.c_str());
    unique_ptr<Stmt> source;
    if (table == nullptr) {
      const ViewDefinition *view = db->find_view(relation.name);
      if (view == nullptr) {
        return RC::SCHEMA_TABLE_NOT_EXIST;
      }
      RC rc = db->prepare_view_query(view->query, source);
      if (OB_FAIL(rc)) {
        return rc;
      }
      table = view->schema;
    }
    view_sources.push_back(std::move(source));
    RC rc = binder_context.add_table(table, relation.alias);
    if (OB_FAIL(rc)) {
      return rc;
    }
    tables.push_back(table);
    qualifiers.push_back(relation.alias);
  }

  // collect query fields in `select` statement
  vector<unique_ptr<Expression>> bound_expressions;
  ExpressionBinder expression_binder(binder_context);
  
  for (unique_ptr<Expression> &expression : select_sql.expressions) {
    RC rc = expression_binder.bind_expression(expression, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> group_by_expressions;
  for (unique_ptr<Expression> &expression : select_sql.group_by) {
    RC rc = expression_binder.bind_expression(expression, group_by_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<OrderBySqlNode> order_by;
  for (auto &item : select_sql.order_by) {
    auto &key = item.key;
    if (key->type() == ExprType::UNBOUND_FIELD) {
      const auto &name = static_cast<UnboundFieldExpr &>(*key);
      if (*name.table_name() == '\0') {
        for (const auto &column : bound_expressions) {
          if (*column->alias() && strcasecmp(column->alias(), name.field_name()) == 0) {
            key = column->copy();
            key->set_name(column->name());
            order_by.push_back({std::move(key), item.ascending});
            break;
          }
        }
        if (!key) {
          continue;
        }
      }
    }
    vector<unique_ptr<Expression>> bound;
    RC rc = expression_binder.bind_expression(key, bound);
    if (OB_FAIL(rc)) {
      return rc;
    }
    if (bound.size() != 1) {
      return RC::INVALID_ARGUMENT;
    }
    order_by.push_back({std::move(bound.front()), item.ascending});
  }

  Table *default_table = nullptr;
  if (tables.size() == 1) {
    default_table = tables[0];
  }

  // create filter statement in `where` statement
  FilterStmt *filter_stmt = nullptr;
  RC          rc          = FilterStmt::create(db,
      default_table,
      nullptr,
      select_sql.conditions.data(),
      static_cast<int>(select_sql.conditions.size()),
      filter_stmt, false, &binder_context);
  if (rc != RC::SUCCESS) {
    LOG_WARN("cannot construct filter stmt");
    return rc;
  }

  FilterStmt *having_stmt = nullptr;
  rc = FilterStmt::create(db, default_table, nullptr, select_sql.having.data(),
      static_cast<int>(select_sql.having.size()), having_stmt, true, &binder_context);
  if (OB_FAIL(rc)) {
    delete filter_stmt;
    return rc;
  }

  // everything alright
  SelectStmt *select_stmt = new SelectStmt();

  select_stmt->tables_.swap(tables);
  select_stmt->qualifiers_.swap(qualifiers);
  select_stmt->view_sources_.swap(view_sources);
  select_stmt->query_expressions_.swap(bound_expressions);
  select_stmt->filter_stmt_ = filter_stmt;
  select_stmt->having_stmt_ = having_stmt;
  select_stmt->group_by_.swap(group_by_expressions);
  select_stmt->order_by_.swap(order_by);
  select_stmt->limit_ = select_sql.limit;
  if (correlated != nullptr) {
    *correlated = binder_context.correlated();
  }
  stmt = select_stmt;
  return RC::SUCCESS;
}
