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

#include "sql/stmt/filter_stmt.h"
#include "sql/stmt/stmt.h"

class Table;
class FieldMeta;

struct UpdateColumnValue
{
  const FieldMeta *field;
  shared_ptr<Expression> expression;
};

class UpdateStmt final : public Stmt
{
public:
  UpdateStmt(Table *table, vector<UpdateColumnValue> values, unique_ptr<FilterStmt> filter)
      : table_(table), values_(std::move(values)), filter_(std::move(filter)) {}

  static RC create(Db *db, const UpdateSqlNode &update_sql, Stmt *&stmt);
  StmtType type() const override { return StmtType::UPDATE; }

  Table *table() const { return table_; }
  const vector<UpdateColumnValue> &values() const { return values_; }
  FilterStmt *filter_stmt() const { return filter_.get(); }

private:
  Table *table_;
  vector<UpdateColumnValue> values_;
  unique_ptr<FilterStmt> filter_;
};
