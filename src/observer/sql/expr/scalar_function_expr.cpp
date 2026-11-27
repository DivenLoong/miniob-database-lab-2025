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

#include "storage/index/full_text_index.h"
#include "storage/table/table.h"

namespace {
bool is_vector_parse(const char *name)
{
  return strcasecmp(name, "string_to_vector") == 0 || strcasecmp(name, "to_vector") == 0;
}
bool is_vector_format(const char *name)
{
  return strcasecmp(name, "vector_to_string") == 0 || strcasecmp(name, "from_vector") == 0;
}
bool is_vector_distance(const char *name)
{
  return strcasecmp(name, "distance") == 0 || strcasecmp(name, "vector_distance") == 0;
}
}

bool ScalarFunctionExpr::supports(const char *name)
{
  return strcasecmp(name, "length") == 0 || strcasecmp(name, "round") == 0 || strcasecmp(name, "date_format") == 0 || strcasecmp(name, "tokenize") == 0 ||
      strcasecmp(name, "match") == 0 || is_vector_parse(name) || is_vector_format(name) || is_vector_distance(name);
}

AttrType ScalarFunctionExpr::value_type() const
{
  if (strcasecmp(function_.c_str(), "length") == 0) {
    return AttrType::INTS;
  }
  if (is_vector_parse(function_.c_str())) {
    return AttrType::VECTORS;
  }
  return strcasecmp(function_.c_str(), "round") == 0 || strcasecmp(function_.c_str(), "match") == 0 ||
      is_vector_distance(function_.c_str()) ?
      AttrType::FLOATS : AttrType::CHARS;
}

RC ScalarFunctionExpr::validate_arguments() const
{
  const size_t count = arguments_.size();
  const auto accepts = [&](size_t index, AttrType type) {
    return arguments_[index]->value_type() == type || arguments_[index]->value_type() == AttrType::NULLS;
  };
  if (strcasecmp(function_.c_str(), "tokenize") == 0) {
    return count == 2 && accepts(0, AttrType::CHARS) && accepts(1, AttrType::CHARS) ?
        RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  if (strcasecmp(function_.c_str(), "match") == 0) {
    return count == 2 && arguments_[0]->type() == ExprType::FIELD &&
        accepts(0, AttrType::CHARS) && accepts(1, AttrType::CHARS) ?
        RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  if (is_vector_distance(function_.c_str())) {
    return count == 3 && accepts(0, AttrType::VECTORS) && accepts(1, AttrType::VECTORS) &&
        accepts(2, AttrType::CHARS) ? RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  if (is_vector_parse(function_.c_str())) {
    return count == 1 && accepts(0, AttrType::CHARS) ? RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  if (is_vector_format(function_.c_str())) {
    return count == 1 && accepts(0, AttrType::VECTORS) ? RC::SUCCESS : RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
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

  if (strcasecmp(function_.c_str(), "tokenize") == 0) {
    if (strcasecmp(args[1].get_string().c_str(), "jieba") != 0) {
      return RC::INVALID_ARGUMENT;
    }
    vector<string> tokens;
    RC rc = FullTextIndex::tokenize(args[0].get_string(), tokens);
    if (OB_FAIL(rc)) {
      return rc;
    }
    string output = "[";
    for (size_t i = 0; i < tokens.size(); ++i) {
      if (i != 0) {
        output += ", ";
      }
      output += '"';
      for (char character : tokens[i]) {
        if (character == '"' || character == '\\') {
          output += '\\';
        }
        output += character;
      }
      output += '"';
    }
    output += ']';
    result.set_string(output.c_str(), output.size());
    return RC::SUCCESS;
  }
  if (strcasecmp(function_.c_str(), "match") == 0) {
    const FieldExpr &field = static_cast<const FieldExpr &>(*arguments_[0]);
    auto *index = static_cast<FullTextIndex *>(field.field().table()->find_full_text(field.field_name()));
    if (index == nullptr) {
      return RC::NOTFOUND;
    }
    float score = 0;
    RC rc = index->score(args[0].get_string(), args[1].get_string(), score);
    if (OB_SUCC(rc)) {
      result.set_float(score);
    }
    return rc;
  }
  if (is_vector_parse(function_.c_str())) {
    return Value::cast_to(args[0], AttrType::VECTORS, result);
  }
  if (is_vector_format(function_.c_str())) {
    const string formatted = args[0].get_string();
    result.set_string(formatted.c_str(), formatted.size());
    return RC::SUCCESS;
  }
  if (is_vector_distance(function_.c_str())) {
    const auto left = args[0].get_vector();
    const auto right = args[1].get_vector();
    if (left.empty() || left.size() != right.size()) {
      return RC::INVALID_ARGUMENT;
    }
    double product = 0.0;
    double squared_a = 0.0;
    double squared_b = 0.0;
    double squared_distance = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
      const double a = left[i];
      const double b = right[i];
      product += a * b;
      squared_a += a * a;
      squared_b += b * b;
      squared_distance += (a - b) * (a - b);
    }
    const string metric = args[2].get_string();
    if (strcasecmp(metric.c_str(), "DOT") == 0) {
      result.set_float(product);
    } else if (strcasecmp(metric.c_str(), "EUCLIDEAN") == 0) {
      result.set_float(std::sqrt(squared_distance));
    } else if (strcasecmp(metric.c_str(), "COSINE") == 0) {
      if (squared_a == 0.0 || squared_b == 0.0) {
        result.set_null();
      } else {
        const double similarity = std::clamp(product / std::sqrt(squared_a * squared_b), -1.0, 1.0);
        result.set_float(1.0 - similarity);
      }
    } else {
      return RC::INVALID_ARGUMENT;
    }
    return RC::SUCCESS;
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
