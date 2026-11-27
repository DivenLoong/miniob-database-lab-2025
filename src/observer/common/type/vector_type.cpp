/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/vector_type.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <string_view>

#include "common/lang/vector.h"
#include "common/value.h"

RC VectorType::set_value_from_str(Value &value, const string &source) const
{
  std::string_view input(source);
  const auto trim = [](std::string_view &part) {
    const size_t start = part.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
      part = {};
    } else {
      part = part.substr(start, part.find_last_not_of(" \t\r\n") - start + 1);
    }
  };
  trim(input);
  if (input.size() < 3 || input.front() != '[' || input.back() != ']') {
    return RC::INVALID_ARGUMENT;
  }
  input.remove_prefix(1);
  input.remove_suffix(1);

  vector<float> values;
  for (;;) {
    const size_t comma = input.find(',');
    auto item = input.substr(0, comma);
    trim(item);
    if (!item.empty() && item.front() == '+') {
      item.remove_prefix(1);
    }
    if (item.empty() || values.size() == 16383) {
      return RC::INVALID_ARGUMENT;
    }
    float component = 0;
    auto parsed = std::from_chars(item.data(), item.data() + item.size(), component);
    if (parsed.ec != std::errc{} || parsed.ptr != item.data() + item.size() || !std::isfinite(component)) {
      return RC::INVALID_ARGUMENT;
    }
    values.push_back(component);
    if (comma == std::string_view::npos) {
      break;
    }
    input.remove_prefix(comma + 1);
  }
  value.set_vector(values);
  return RC::SUCCESS;
}

int VectorType::compare(const Value &left, const Value &right) const
{
  const auto a = left.get_vector();
  const auto b = right.get_vector();
  if (std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end())) {
    return -1;
  }
  return std::equal(a.begin(), a.end(), b.begin(), b.end()) ? 0 : 1;
}

RC VectorType::to_string(const Value &value, string &result) const
{
  const auto values = value.get_vector();
  std::ostringstream out;
  out << std::scientific << std::setprecision(5) << '[';
  for (size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    out << values[i];
  }
  out << ']';
  result = out.str();
  return RC::SUCCESS;
}
