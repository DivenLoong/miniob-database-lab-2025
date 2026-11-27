/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/index/index_meta.h"

#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

RC IndexMeta::init(const char *name, const FieldMeta &field)
{
  return init(name, {&field}, false);
}

RC IndexMeta::init(const char *name, const vector<const FieldMeta *> &fields, bool unique)
{
  if (common::is_blank(name) || fields.empty()) {
    return RC::INVALID_ARGUMENT;
  }
  name_ = name;
  unique_ = unique;
  fields_.clear();
  for (const FieldMeta *field : fields) {
    fields_.emplace_back(field->name());
  }
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value["name"] = name_;
  json_value["field_name"] = fields_.front();
  if (fields_.size() > 1) {
    Json::Value fields(Json::arrayValue);
    for (const auto &field : fields_) {
      fields.append(field);
    }
    json_value["fields"] = std::move(fields);
  }
  json_value["unique"] = unique_;
  if (full_text_) {
    json_value["full_text"] = true;
  }
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  if (!json_value["name"].isString()) {
    return RC::INTERNAL;
  }
  vector<const FieldMeta *> fields;
  const auto &items = json_value["fields"];
  if (items.isArray()) {
    for (const auto &item : items) {
      if (!item.isString()) {
        return RC::INTERNAL;
      }
      const FieldMeta *field = table.field(item.asCString());
      if (field == nullptr) {
        return RC::SCHEMA_FIELD_MISSING;
      }
      fields.push_back(field);
    }
  } else {
    const auto &item = json_value["field_name"];
    if (!item.isString()) {
      return RC::INTERNAL;
    }
    const FieldMeta *field = table.field(item.asCString());
    if (field == nullptr) {
      return RC::SCHEMA_FIELD_MISSING;
    }
    fields.push_back(field);
  }
  RC rc = index.init(json_value["name"].asCString(), fields, json_value.get("unique", false).asBool());
  index.set_full_text(json_value.get("full_text", false).asBool());
  return rc;
}

const char *IndexMeta::name() const { return name_.c_str(); }
const char *IndexMeta::field() const { return fields_.front().c_str(); }
void IndexMeta::desc(ostream &os) const
{
  os << "index name=" << name_ << ", fields=";
  for (size_t i = 0; i < fields_.size(); ++i) {
    if (i != 0) {
      os << ',';
    }
    os << fields_[i];
  }
}
