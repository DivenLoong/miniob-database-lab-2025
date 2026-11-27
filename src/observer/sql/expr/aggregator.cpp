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
// Created by Wangyunlai on 2024/05/29.
//

#include "sql/expr/aggregator.h"
#include "common/log/log.h"

RC CountAggregator::accumulate(const Value &value)
{
  if (!value.is_null()) {
    ++count_;
  }
  return RC::SUCCESS;
}

RC CountAggregator::evaluate(Value &result)
{
  result.set_int(count_);
  return RC::SUCCESS;
}

RC AverageAggregator::accumulate(const Value &value)
{
  if (!value.is_null()) {
    sum_ += value.get_float();
    ++count_;
  }
  return RC::SUCCESS;
}

RC AverageAggregator::evaluate(Value &result)
{
  if (count_ == 0) {
    result.set_null();
  } else {
    result.set_float(static_cast<float>(sum_ / count_));
  }
  return RC::SUCCESS;
}

RC ExtremumAggregator::accumulate(const Value &value)
{
  if (value.is_null()) {
    return RC::SUCCESS;
  }
  if (value_.attr_type() == AttrType::UNDEFINED ||
      (maximum_ ? value.compare(value_) > 0 : value.compare(value_) < 0)) {
    value_ = value;
  }
  return RC::SUCCESS;
}

RC ExtremumAggregator::evaluate(Value &result)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    result.set_null();
  } else {
    result = value_;
  }
  return RC::SUCCESS;
}

RC SumAggregator::accumulate(const Value &value)
{
  if (value.is_null()) {
    return RC::SUCCESS;
  }
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
    return RC::SUCCESS;
  }
  
  ASSERT(value.attr_type() == value_.attr_type(), "type mismatch. value type: %s, value_.type: %s", 
        attr_type_to_string(value.attr_type()), attr_type_to_string(value_.attr_type()));
  
  Value::add(value, value_, value_);
  return RC::SUCCESS;
}

RC SumAggregator::evaluate(Value& result)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    result.set_null();
  } else {
    result = value_;
  }
  return RC::SUCCESS;
}
