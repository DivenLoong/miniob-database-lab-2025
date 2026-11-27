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
#include <cstdint>

namespace {
constexpr size_t max_rows_per_run = 2048;
constexpr size_t max_bytes_per_run = 12 * 1024 * 1024;

bool write_values(FILE *file, const vector<Value> &values)
{
  for (const Value &value : values) {
    const uint8_t type = static_cast<uint8_t>(value.attr_type());
    const uint32_t length = value.length();
    if (fwrite(&type, sizeof(type), 1, file) != 1 || fwrite(&length, sizeof(length), 1, file) != 1 ||
        (length > 0 && fwrite(value.data(), 1, length, file) != length)) {
      return false;
    }
  }
  return true;
}

bool read_values(FILE *file, int count, vector<Value> &values)
{
  values.clear();
  values.reserve(count);
  for (int i = 0; i < count; ++i) {
    uint8_t type_byte = 0;
    uint32_t length = 0;
    if (fread(&type_byte, sizeof(type_byte), 1, file) != 1 || fread(&length, sizeof(length), 1, file) != 1 ||
        type_byte >= static_cast<uint8_t>(AttrType::MAXTYPE) || length > 1024 * 1024) {
      return false;
    }
    std::string bytes(length, '\0');
    if (length > 0 && fread(bytes.data(), 1, length, file) != length) {
      return false;
    }
    Value value;
    const auto type = static_cast<AttrType>(type_byte);
    if (type == AttrType::NULLS) {
      value.set_null();
    } else {
      value.set_type(type);
      value.set_data(bytes.data(), length);
    }
    values.emplace_back(std::move(value));
  }
  return true;
}
}

SortPhysicalOperator::SortPhysicalOperator(vector<OrderBySqlNode> keys, int limit)
    : keys_(std::move(keys)), limit_(limit), queue_(Later{this})
{}

int SortPhysicalOperator::compare(const Row &left, const Row &right) const
{
  for (size_t i = 0; i < keys_.size(); ++i) {
    const int order = left.keys[i].compare(right.keys[i]);
    if (order != 0) {
      return keys_[i].ascending ? order : (order < 0 ? 1 : -1);
    }
  }
  return 0;
}

bool SortPhysicalOperator::Later::operator()(size_t left, size_t right) const
{
  const int order = owner->compare(owner->runs_[left].head, owner->runs_[right].head);
  return order == 0 ? left > right : order > 0;
}

void SortPhysicalOperator::sort_rows()
{
  std::stable_sort(rows_.begin(), rows_.end(), [&](const Row &left, const Row &right) {
    return compare(left, right) < 0;
  });
}

RC SortPhysicalOperator::write_run()
{
  sort_rows();
  unique_ptr<FILE, decltype(&fclose)> file(tmpfile(), &fclose);
  if (!file) {
    return RC::IOERR_OPEN;
  }
  for (const auto &row : rows_) {
    if (!write_values(file.get(), row.keys) || !write_values(file.get(), row.cells)) {
      return RC::IOERR_WRITE;
    }
  }
  if (fflush(file.get()) != 0 || fseek(file.get(), 0, SEEK_SET) != 0) {
    return RC::IOERR_WRITE;
  }
  runs_.push_back({std::move(file), rows_.size(), {}});
  rows_.clear();
  return RC::SUCCESS;
}

RC SortPhysicalOperator::read_head(Run &run)
{
  if (!read_values(run.file.get(), key_count_, run.head.keys) || !read_values(run.file.get(), cell_count_, run.head.cells)) {
    return RC::IOERR_READ;
  }
  --run.remaining;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::open(Trx *trx)
{
  emitted_ = 0;
  rows_.clear();
  runs_.clear();
  queue_ = decltype(queue_)(Later{this});
  key_count_ = keys_.size();
  cell_count_ = 0;
  PhysicalOperator &child = *children_.front();
  RC rc = child.open(trx);
  if (OB_FAIL(rc)) {
    return rc;
  }
  child_open_ = true;
  if (keys_.empty() || limit_ == 0) {
    return RC::SUCCESS;
  }

  vector<TupleCellSpec> schema;
  bool captured_schema = false;
  size_t used_bytes = 0;
  while ((rc = child.next()) == RC::SUCCESS) {
    Tuple *tuple = child.current_tuple();
    if (tuple == nullptr) {
      return RC::INTERNAL;
    }
    if (!captured_schema) {
      cell_count_ = tuple->cell_num();
      for (int i = 0; i < cell_count_; ++i) {
        TupleCellSpec spec;
        rc = tuple->spec_at(i, spec);
        if (OB_FAIL(rc)) {
          return rc;
        }
        schema.emplace_back(std::move(spec));
      }
      output_.set_names(schema);
      captured_schema = true;
    }

    Row row;
    row.cells.resize(cell_count_);
    row.keys.resize(key_count_);
    used_bytes += sizeof(Row) + (cell_count_ + key_count_) * sizeof(Value);
    for (int i = 0; i < cell_count_; ++i) {
      rc = tuple->cell_at(i, row.cells[i]);
      if (OB_FAIL(rc)) {
        return rc;
      }
      used_bytes += row.cells[i].length();
    }
    for (size_t i = 0; i < key_count_; ++i) {
      rc = keys_[i].key->get_value(*tuple, row.keys[i]);
      if (OB_FAIL(rc)) {
        return rc;
      }
      used_bytes += row.keys[i].length();
    }
    rows_.push_back(std::move(row));

    if (rows_.size() >= max_rows_per_run || used_bytes >= max_bytes_per_run) {
      rc = write_run();
      if (OB_FAIL(rc)) {
        return rc;
      }
      used_bytes = 0;
    }
  }
  if (rc != RC::RECORD_EOF) {
    return rc;
  }
  rc = child.close();
  child_open_ = false;
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (runs_.empty()) {
    sort_rows();
    return RC::SUCCESS;
  }
  if (!rows_.empty()) {
    rc = write_run();
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  for (size_t i = 0; i < runs_.size(); ++i) {
    rc = read_head(runs_[i]);
    if (OB_FAIL(rc)) {
      return rc;
    }
    queue_.push(i);
  }
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
  if (runs_.empty()) {
    if (emitted_ >= rows_.size()) {
      return RC::RECORD_EOF;
    }
    output_.set_cells(std::move(rows_[emitted_].cells));
  } else {
    if (queue_.empty()) {
      return RC::RECORD_EOF;
    }
    const size_t index = queue_.top();
    queue_.pop();
    Run &run = runs_[index];
    output_.set_cells(std::move(run.head.cells));
    if (run.remaining > 0) {
      RC rc = read_head(run);
      if (OB_FAIL(rc)) {
        return rc;
      }
      queue_.push(index);
    }
  }
  ++emitted_;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::close()
{
  rows_.clear();
  runs_.clear();
  queue_ = decltype(queue_)(Later{this});
  if (child_open_) {
    child_open_ = false;
    return children_.front()->close();
  }
  return RC::SUCCESS;
}

Tuple *SortPhysicalOperator::current_tuple()
{
  return keys_.empty() ? children_.front()->current_tuple() : &output_;
}
