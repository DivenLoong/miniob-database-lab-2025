/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/date_type.h"

#include <charconv>
#include <chrono>
#include <cstdio>

#include "common/value.h"

int DateType::compare(const Value &left, const Value &right) const
{
  return (left.get_int() > right.get_int()) - (left.get_int() < right.get_int());
}

RC DateType::set_value_from_str(Value &value, const string &text) const
{
  const auto first = text.find('-');
  if (first == string::npos) {
    return RC::INVALID_ARGUMENT;
  }
  const auto second = text.find('-', first + 1);
  if (second == string::npos || first == 0 || first > 4 ||
      second <= first + 1 || second - first > 3 || text.size() - second > 3 || text.size() == second + 1) {
    return RC::INVALID_ARGUMENT;
  }
  const auto parse = [&](size_t begin, size_t end, unsigned &out) {
    const char *finish = text.data() + end;
    const auto converted = std::from_chars(text.data() + begin, finish, out);
    return converted.ec == std::errc{} && converted.ptr == finish;
  };
  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (!parse(0, first, year) || !parse(first + 1, second, month) ||
      !parse(second + 1, text.size(), day) || year == 0) {
    return RC::INVALID_ARGUMENT;
  }
  const std::chrono::year_month_day date{
      std::chrono::year(static_cast<int>(year)), std::chrono::month(month), std::chrono::day(day)};
  if (!date.ok()) {
    return RC::INVALID_ARGUMENT;
  }
  value.set_int(year * 10000 + month * 100 + day);
  value.set_type(AttrType::DATES);
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &value, string &result) const
{
  const int date = value.get_int();
  char text[16];
  std::snprintf(text, sizeof(text), "%04u-%02u-%02u",
      static_cast<unsigned>(date / 10000) % 10000, static_cast<unsigned>(date / 100) % 100,
      static_cast<unsigned>(date) % 100);
  result = text;
  return RC::SUCCESS;
}
