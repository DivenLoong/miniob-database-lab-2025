/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by wangyunlai.wyl on 2021/5/19.
//

#pragma once

#include <mutex>

#include "storage/index/bplus_tree.h"
#include "storage/index/index.h"

/**
 * @brief B+树索引
 * @ingroup Index
 */
class BplusTreeIndex : public Index
{
public:
  BplusTreeIndex() = default;
  virtual ~BplusTreeIndex() noexcept;

  RC create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC close();

  RC insert_entry(const char *record, const RID *rid) override;
  RC delete_entry(const char *record, const RID *rid) override;
  bool same_key(const char *left, const char *right) const override;

  /**
   * 扫描指定范围的数据
   */
  IndexScanner *create_scanner(const char *left_key, int left_len, bool left_inclusive, const char *right_key,
      int right_len, bool right_inclusive) override;

  RC sync() override;

private:
  RC init_key(Table *table, const IndexMeta &meta);
  string pack_key(const char *record, bool &has_null) const;
  void encode_part(char *destination, const FieldMeta &field, const char *source, size_t size) const;
  string scan_bound(const char *key, int len, unsigned char suffix) const;

  vector<FieldMeta> key_fields_;
  int packed_size_ = 0;
  std::mutex uniqueness_mutex_;
  bool             inited_ = false;
  Table           *table_  = nullptr;
  BplusTreeHandler index_handler_;
};

/**
 * @brief B+树索引扫描器
 * @ingroup Index
 */
class BplusTreeIndexScanner : public IndexScanner
{
public:
  BplusTreeIndexScanner(BplusTreeHandler &tree_handle);
  ~BplusTreeIndexScanner() noexcept override;

  RC next_entry(RID *rid) override;
  RC destroy() override;

  RC open(const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len,
      bool right_inclusive);

private:
  BplusTreeScanner tree_scanner_;
};
