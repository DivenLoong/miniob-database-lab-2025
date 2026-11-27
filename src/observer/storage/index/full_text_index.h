/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <mutex>
#include <unordered_map>

#include "storage/index/index.h"

class FullTextIndex final : public Index
{
public:
  RC create(Table *table, const char *file_name, const IndexMeta &meta, const FieldMeta &field) override;
  RC open(Table *table, const char *file_name, const IndexMeta &meta, const FieldMeta &field) override;
  RC insert_entry(const char *record, const RID *rid) override;
  RC delete_entry(const char *record, const RID *rid) override;
  IndexScanner *create_scanner(const char *, int, bool, const char *, int, bool) override { return nullptr; }
  RC sync() override { return RC::SUCCESS; }

  static RC tokenize(string text, vector<string> &tokens);
  RC score(const string &text, const string &query, float &result) const;

private:
  struct Document
  {
    size_t length = 0;
    vector<string> terms;
  };
  using Posting = std::unordered_map<RID, unsigned, RIDHash>;
  void remove_locked(const RID &rid);
  double inverse_frequency(size_t document_frequency) const;

  Table *table_ = nullptr;
  std::unordered_map<string, Posting> postings_;
  std::unordered_map<RID, Document, RIDHash> documents_;
  size_t total_length_ = 0;
  mutable std::mutex mutex_;
  mutable bool idf_dirty_ = true;
  mutable double average_idf_ = 0;
};
