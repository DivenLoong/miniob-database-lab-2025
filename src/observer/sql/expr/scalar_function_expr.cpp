/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/expr/scalar_function_expr.h"

#include <algorithm>
#include <cmath>

AttrType ScalarFunctionExpr::value_type() const
{
  if (strcasecmp(function_.c_str(), "length") == 0) {
    return AttrType::INTS;
  }
  return strcasecmp(function_.c_str(), "round") == 0 ? AttrType::FLOATS : AttrType::CHARS;
}

RC ScalarFunctionExpr::validate_arguments() const
{
  const size_t count = arguments_.size();
  const auto accepts = [&](size_t index, AttrType type) {
    return arguments_[index]->value_type() == type || arguments_[index]->value_type() == AttrType::NULLS;
  };
  switch (value_type()) {
    case AttrType::INTS:
      return count == 1 && accepts(0, AttrType::CHARS) ? RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
    case AttrType::FLOATS:
      return (count == 1 || count == 2) && accepts(0, AttrType::FLOATS) &&
             (count == 1 || accepts(1, AttrType::INTS)) ? RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
    default:
      return count == 2 && (accepts(0, AttrType::DATES) ||
            (arguments_[0]->type() == ExprType::VALUE && accepts(0, AttrType::CHARS))) &&
            accepts(1, AttrType::CHARS) ? RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
}

unique_ptr<Expression> ScalarFunctionExpr::copy() const
{
  vector<unique_ptr<Expression>> arguments;
  for (const auto &arg : arguments_) {
    arguments.emplace_back(arg->copy());
  }
  auto result = make_unique<ScalarFunctionExpr>(function_, std::move(arguments));
  result->set_name(name());
  return result;
}

RC ScalarFunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  vector<Value> args(arguments_.size());
  for (size_t i = 0; i < args.size(); ++i) {
    RC rc = arguments_[i]->get_value(tuple, args[i]);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  return evaluate(args, value);
}

RC ScalarFunctionExpr::try_get_value(Value &value) const
{
  vector<Value> args(arguments_.size());
  for (size_t i = 0; i < args.size(); ++i) {
    RC rc = arguments_[i]->try_get_value(args[i]);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  return evaluate(args, value);
}

RC ScalarFunctionExpr::evaluate(const vector<Value> &args, Value &result) const
{
  for (const Value &arg : args) {
    if (arg.is_null()) {
      result.set_null();
      return RC::SUCCESS;
    }
  }

  if (value_type() == AttrType::INTS) {
    result.set_int(args[0].length());
    return RC::SUCCESS;
  }

  if (value_type() == AttrType::FLOATS) {
    const int places = args.size() == 1 ? 0 : std::clamp(args[1].get_int(), -38, 38);
    const double scale = std::pow(10.0, places);
    const double original = args[0].get_float();
    // SQL float input is already rounded to binary32 before the function receives it.
    const double shifted = original * scale;
    const double correction = std::abs(shifted) * 0.00000003;
    result.set_float(static_cast<float>(std::round(shifted + std::copysign(correction, shifted)) / scale));
    return RC::SUCCESS;
  }

  static const char *months[] = {"January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};
  Value date = args[0];
  if (date.attr_type() != AttrType::DATES) {
    RC rc = Value::cast_to(args[0], AttrType::DATES, date);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  const string digits = date.get_string();
  const int month = date.get_int() / 100 % 100;
  const int day = date.get_int() % 100;
  const string format = args[1].get_string();
  string output;
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%' || i + 1 == format.size()) {
      output += format[i];
      continue;
    }
    switch (format[++i]) {
      case 'Y': output += digits.substr(0, 4); break;
      case 'y': output += digits.substr(2, 2); break;
      case 'm': output += digits.substr(5, 2); break;
      case 'd': output += digits.substr(8, 2); break;
      case 'M': output += months[month - 1]; break;
      case 'D': {
        output += std::to_string(day);
        const int last = day % 10;
        output += day >= 11 && day <= 13 ? "th" : last == 1 ? "st" : last == 2 ? "nd" : last == 3 ? "rd" : "th";
        break;
      }
      default: output += format[i]; break;
    }
  }
  result.set_string(output.c_str());
  return RC::SUCCESS;
}
