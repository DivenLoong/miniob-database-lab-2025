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

#include "sql/stmt/stmt.h"

class Table;
class FieldMeta;

class CreateIndexStmt final : public Stmt
{
public:
  CreateIndexStmt(Table *table, vector<const FieldMeta *> fields, const string &index_name, bool unique, bool full_text)
      : table_(table), fields_(std::move(fields)), index_name_(index_name), unique_(unique), full_text_(full_text) {}

  StmtType type() const override { return StmtType::CREATE_INDEX; }
  Table *table() const { return table_; }
  const vector<const FieldMeta *> &fields() const { return fields_; }
  const string &index_name() const { return index_name_; }
  bool unique() const { return unique_; }
  bool full_text() const { return full_text_; }
  static RC create(Db *db, const CreateIndexSqlNode &create_index, Stmt *&stmt);

private:
  Table *table_;
  vector<const FieldMeta *> fields_;
  string index_name_;
  bool unique_;
  bool full_text_;
};
