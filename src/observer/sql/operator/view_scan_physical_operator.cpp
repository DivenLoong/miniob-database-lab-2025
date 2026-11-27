/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/view_scan_physical_operator.h"

#include "storage/table/table.h"

ViewScanPhysicalOperator::ViewScanPhysicalOperator(const Table *schema, const string &qualifier)
    : width_(schema->table_meta().field_num())
{
  vector<TupleCellSpec> names;
  for (int i = 0; i < width_; ++i) {
    names.emplace_back(qualifier.empty() ? schema->name() : qualifier.c_str(), schema->table_meta().field(i)->name());
  }
  tuple_.set_names(names);
}

RC ViewScanPhysicalOperator::next()
{
  RC rc = children_[0]->next();
  if (OB_FAIL(rc)) {
    return rc;
  }
  Tuple *input = children_[0]->current_tuple();
  if (input->cell_num() != width_) {
    return RC::SCHEMA_FIELD_MISSING;
  }
  vector<Value> values(width_);
  for (int i = 0; i < width_; ++i) {
    rc = input->cell_at(i, values[i]);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  tuple_.set_cells(std::move(values));
  return RC::SUCCESS;
}
