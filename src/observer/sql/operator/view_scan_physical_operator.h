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

#include "sql/operator/physical_operator.h"

class ViewScanPhysicalOperator final : public PhysicalOperator
{
public:
  ViewScanPhysicalOperator(const Table *schema, const string &qualifier);
  PhysicalOperatorType type() const override { return PhysicalOperatorType::VIEW_SCAN; }
  RC open(Trx *trx) override { return children_[0]->open(trx); }
  RC next() override;
  RC close() override { return children_[0]->close(); }
  Tuple *current_tuple() override { return &tuple_; }

private:
  int width_;
  ValueListTuple tuple_;
};
