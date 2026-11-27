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

#include <cstdio>
#include <queue>

#include "sql/operator/physical_operator.h"

class SortPhysicalOperator final : public PhysicalOperator
{
public:
  SortPhysicalOperator(vector<OrderBySqlNode> keys, int limit);
  PhysicalOperatorType type() const override { return PhysicalOperatorType::SORT; }
  OpType get_op_type() const override { return OpType::ORDERBY; }
  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;
  Tuple *current_tuple() override;

private:
  struct Row
  {
    vector<Value> cells;
    vector<Value> keys;
  };
  struct Run
  {
    unique_ptr<FILE, decltype(&fclose)> file;
    size_t remaining;
    Row head;
  };
  struct Later
  {
    const SortPhysicalOperator *owner;
    bool operator()(size_t left, size_t right) const;
  };

  int compare(const Row &left, const Row &right) const;
  void sort_rows();
  RC write_run();
  RC read_head(Run &run);

  vector<OrderBySqlNode> keys_;
  int limit_;
  vector<Row> rows_;
  vector<Run> runs_;
  std::priority_queue<size_t, vector<size_t>, Later> queue_;
  ValueListTuple output_;
  size_t key_count_ = 0;
  int cell_count_ = 0;
  size_t emitted_ = 0;
  bool child_open_ = false;
};
