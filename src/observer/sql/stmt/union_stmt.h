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

class UnionStmt final : public Stmt
{
public:
  UnionStmt(unique_ptr<Stmt> left, unique_ptr<Stmt> right, bool all)
      : left_(std::move(left)), right_(std::move(right)), all_(all) {}
  StmtType type() const override { return StmtType::UNION_SELECT; }
  Stmt *left() const { return left_.get(); }
  Stmt *right() const { return right_.get(); }
  bool all() const { return all_; }

private:
  unique_ptr<Stmt> left_;
  unique_ptr<Stmt> right_;
  bool all_;
};
