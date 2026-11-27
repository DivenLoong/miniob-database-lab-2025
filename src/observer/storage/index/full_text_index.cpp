/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/index/full_text_index.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>

#include "storage/table/table.h"
#include "storage/tokenizer/jieba_tokenizer.h"

RC FullTextIndex::tokenize(string text, vector<string> &tokens)
{
  static JiebaTokenizer tokenizer;
  static std::mutex tokenizer_mutex;
  std::lock_guard<std::mutex> guard(tokenizer_mutex);
  RC rc = tokenizer.cut(text, tokens);
  if (OB_FAIL(rc)) {
    return rc;
  }
  std::erase_if(tokens, [](const string &token) {
    if (token == "，" || token == "。" || token == "？" || token == "！" ||
        token == "、" || token == "：" || token == "；" || token == "　") {
      return true;
    }
    return std::all_of(token.begin(), token.end(), [](unsigned char character) {
      return character < 128 && (std::isspace(character) || std::ispunct(character));
    });
  });
  return RC::SUCCESS;
}

RC FullTextIndex::create(Table *table, const char *file_name, const IndexMeta &meta, const FieldMeta &field)
{
  if (field.type() != AttrType::CHARS && field.type() != AttrType::TEXTS) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  int descriptor = ::open(file_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    return RC::IOERR_OPEN;
  }
  ::close(descriptor);
  table_ = table;
  return Index::init(meta, field);
}

RC FullTextIndex::open(Table *table, const char *file_name, const IndexMeta &meta, const FieldMeta &field)
{
  if (access(file_name, F_OK) != 0) {
    return RC::IOERR_OPEN;
  }
  table_ = table;
  return Index::init(meta, field);
}

void FullTextIndex::remove_locked(const RID &rid)
{
  const auto document = documents_.find(rid);
  if (document == documents_.end()) {
    return;
  }
  for (const string &term : document->second.terms) {
    auto posting = postings_.find(term);
    if (posting != postings_.end()) {
      posting->second.erase(rid);
      if (posting->second.empty()) {
        postings_.erase(posting);
      }
    }
  }
  total_length_ -= document->second.length;
  documents_.erase(document);
  idf_dirty_ = true;
}

RC FullTextIndex::insert_entry(const char *record, const RID *rid)
{
  Value text;
  RC rc = table_->read_value(field_meta_, record, text);
  if (OB_FAIL(rc)) {
    return rc;
  }
  vector<string> tokens;
  if (!text.is_null()) {
    rc = tokenize(text.get_string(), tokens);
    if (OB_FAIL(rc)) {
      return rc;
    }
  }
  std::unordered_map<string, unsigned> counts;
  for (const auto &token : tokens) {
    ++counts[token];
  }

  std::lock_guard<std::mutex> guard(mutex_);
  remove_locked(*rid);
  Document &document = documents_[*rid];
  document.length = tokens.size();
  for (const auto &[term, count] : counts) {
    document.terms.push_back(term);
    postings_[term][*rid] = count;
  }
  total_length_ += document.length;
  idf_dirty_ = true;
  return RC::SUCCESS;
}

RC FullTextIndex::delete_entry(const char *, const RID *rid)
{
  std::lock_guard<std::mutex> guard(mutex_);
  remove_locked(*rid);
  return RC::SUCCESS;
}

double FullTextIndex::inverse_frequency(size_t document_frequency) const
{
  return std::log((documents_.size() - document_frequency + 0.5) / (document_frequency + 0.5));
}

RC FullTextIndex::score(const string &text, const string &query, float &result) const
{
  vector<string> words;
  vector<string> terms;
  RC rc = tokenize(text, words);
  if (OB_SUCC(rc)) {
    rc = tokenize(query, terms);
  }
  if (OB_FAIL(rc)) {
    return rc;
  }

  std::lock_guard<std::mutex> guard(mutex_);
  result = 0;
  if (documents_.empty() || postings_.empty() || total_length_ == 0) {
    return RC::SUCCESS;
  }
  if (idf_dirty_) {
    average_idf_ = 0;
    for (const auto &[token, posting] : postings_) {
      average_idf_ += inverse_frequency(posting.size());
    }
    average_idf_ /= postings_.size();
    idf_dirty_ = false;
  }
  const double average_length = static_cast<double>(total_length_) / documents_.size();
  constexpr double k1 = 1.5;
  constexpr double b = 0.75;
  double sum = 0;
  for (const auto &term : terms) {
    const auto posting = postings_.find(term);
    if (posting == postings_.end()) {
      continue;
    }
    const double frequency = std::count(words.begin(), words.end(), term);
    if (frequency == 0) {
      continue;
    }
    double idf = inverse_frequency(posting->second.size());
    if (idf < 0) {
      idf = 0.25 * average_idf_;
    }
    sum += idf * frequency * (k1 + 1) /
        (frequency + k1 * (1 - b + b * words.size() / average_length));
  }
  result = static_cast<float>(sum);
  return RC::SUCCESS;
}
