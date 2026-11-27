/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/sort_physical_operator.h"

#include <algorithm>

RC SortPhysicalOperator::open(Trx *trx)
{
  emitted_ = 0;
  rows_.clear();
  PhysicalOperator &child = *children_.front();
  RC rc = child.open(trx);
  if (OB_FAIL(rc)) {
    return rc;
  }
  child_open_ = true;

  if (keys_.empty() || limit_ == 0) {
    return RC::SUCCESS;
  }
  while ((rc = child.next()) == RC::SUCCESS) {
    Tuple *tuple = child.current_tuple();
    if (tuple == nullptr) {
      return RC::INTERNAL;
    }
    SortRow row;
    rc = ValueListTuple::make(*tuple, row.tuple);
    if (OB_FAIL(rc)) {
      return rc;
    }
    for (const auto &item : keys_) {
      Value key;
      rc = item.key->get_value(*tuple, key);
      if (OB_FAIL(rc)) {
        return rc;
      }
      row.keys.push_back(std::move(key));
    }
    rows_.push_back(std::move(row));
  }
  if (rc != RC::RECORD_EOF) {
    return rc;
  }
  rc = child.close();
  child_open_ = false;
  if (OB_FAIL(rc)) {
    return rc;
  }

  std::stable_sort(rows_.begin(), rows_.end(), [&](const SortRow &left, const SortRow &right) {
    for (size_t i = 0; i < keys_.size(); ++i) {
      const int order = left.keys[i].compare(right.keys[i]);
      if (order != 0) {
        return keys_[i].ascending ? order < 0 : order > 0;
      }
    }
    return false;
  });
  return RC::SUCCESS;
}

RC SortPhysicalOperator::next()
{
  if (limit_ >= 0 && emitted_ >= static_cast<size_t>(limit_)) {
    return RC::RECORD_EOF;
  }
  if (keys_.empty()) {
    RC rc = children_.front()->next();
    if (OB_SUCC(rc)) {
      ++emitted_;
    }
    return rc;
  }
  if (emitted_ >= rows_.size()) {
    return RC::RECORD_EOF;
  }
  ++emitted_;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::close()
{
  rows_.clear();
  if (child_open_) {
    child_open_ = false;
    return children_.front()->close();
  }
  return RC::SUCCESS;
}

Tuple *SortPhysicalOperator::current_tuple()
{
  if (keys_.empty()) {
    return children_.front()->current_tuple();
  }
  return emitted_ > 0 && emitted_ <= rows_.size() ? &rows_[emitted_ - 1].tuple : nullptr;
}
