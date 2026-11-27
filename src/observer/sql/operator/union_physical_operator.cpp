/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/union_physical_operator.h"

RC UnionPhysicalOperator::open(Trx *trx)
{
  TupleSchema left;
  TupleSchema right;
  RC rc = children_[0]->tuple_schema(left);
  if (OB_FAIL(rc)) {
    return rc;
  }
  rc = children_[1]->tuple_schema(right);
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (left.cell_num() != right.cell_num()) {
    return RC::SCHEMA_FIELD_MISSING;
  }
  trx_ = trx;
  seen_.clear();
  rc = children_[0]->open(trx_);
  if (OB_SUCC(rc)) {
    current_ = 0;
  }
  return rc;
}

RC UnionPhysicalOperator::next()
{
  while (current_ >= 0) {
    RC rc = children_[current_]->next();
    if (rc == RC::RECORD_EOF) {
      const int finished = current_;
      current_ = -1;
      rc = children_[finished]->close();
      if (OB_FAIL(rc)) {
        return rc;
      }
      if (finished == 1) {
        return RC::RECORD_EOF;
      }
      rc = children_[1]->open(trx_);
      if (OB_FAIL(rc)) {
        return rc;
      }
      current_ = 1;
      continue;
    }
    if (OB_FAIL(rc) || all_) {
      return rc;
    }

    const Tuple *tuple = current_tuple();
    string key;
    for (int i = 0; i < tuple->cell_num(); ++i) {
      Value value;
      rc = tuple->cell_at(i, value);
      if (OB_FAIL(rc)) {
        return rc;
      }
      const auto type = static_cast<unsigned char>(value.attr_type());
      const int length = value.length();
      key.push_back(static_cast<char>(type));
      key.append(reinterpret_cast<const char *>(&length), sizeof(length));
      if (value.attr_type() == AttrType::FLOATS && value.get_float() == 0.0f) {
        constexpr float positive_zero = 0.0f;
        key.append(reinterpret_cast<const char *>(&positive_zero), sizeof(positive_zero));
      } else if (length > 0 && !value.is_null()) {
        key.append(value.data(), length);
      }
    }
    if (seen_.insert(std::move(key)).second) {
      return RC::SUCCESS;
    }
  }
  return RC::RECORD_EOF;
}

RC UnionPhysicalOperator::close()
{
  seen_.clear();
  if (current_ < 0) {
    return RC::SUCCESS;
  }
  const int child = current_;
  current_ = -1;
  return children_[child]->close();
}

RC UnionPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  return children_[0]->tuple_schema(schema);
}

Tuple *UnionPhysicalOperator::current_tuple()
{
  return current_ >= 0 ? children_[current_]->current_tuple() : nullptr;
}
