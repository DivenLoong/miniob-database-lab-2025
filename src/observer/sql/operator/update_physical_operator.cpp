/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/update_physical_operator.h"

#include "storage/table/table.h"
#include "storage/trx/trx.h"

RC UpdatePhysicalOperator::open(Trx *trx)
{
  PhysicalOperator &scan = *children_.front();
  RC rc = scan.open(trx);
  if (OB_FAIL(rc)) {
    return rc;
  }

  // Finish scanning before modifying index keys and release any held page latches.
  vector<std::pair<Record, Record>> changes;
  while ((rc = scan.next()) == RC::SUCCESS) {
    const auto &tuple = *static_cast<RowTuple *>(scan.current_tuple());
    const Record &row = tuple.record();
    Record snapshot;
    rc = snapshot.copy_data(row.data(), row.len());
    if (OB_FAIL(rc)) {
      break;
    }
    snapshot.set_rid(row.rid());
    Record changed(snapshot);
    for (const auto &column : values_) {
      Value value;
      rc = column.expression->get_value(tuple, value);
      if (OB_FAIL(rc)) {
        break;
      }
      if (!value.is_null() && value.attr_type() != column.field->type() &&
          !(column.field->type() == AttrType::TEXTS && value.attr_type() == AttrType::CHARS)) {
        Value converted;
        rc = Value::cast_to(value, column.field->type(), converted);
        if (OB_FAIL(rc)) {
          break;
        }
        value = std::move(converted);
      }
      rc = table_->set_value_to_record(changed.data(), value, column.field);
      if (OB_FAIL(rc)) {
        break;
      }
    }
    if (OB_FAIL(rc)) {
      break;
    }
    changes.emplace_back(std::move(snapshot), std::move(changed));
  }
  const RC close_rc = scan.close();
  if (rc != RC::RECORD_EOF) {
    return rc;
  }
  if (OB_FAIL(close_rc)) {
    return close_rc;
  }

  for (auto &[original, changed] : changes) {
    rc = trx->update_record(table_, original, changed);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  return RC::SUCCESS;
}
