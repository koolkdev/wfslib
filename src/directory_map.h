/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "directory_map_iterator.h"

class DirectoryMap {
 public:
  using iterator = DirectoryMapIterator;

  DirectoryMap(std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> root_block);

  size_t size() const { return CalcSizeOfDirectoryBlock(root_block_); }

  iterator begin() const;
  iterator end() const;

  iterator find(std::string_view key, bool exact_match = true) const;

 private:
  size_t CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const;

  std::shared_ptr<QuotaArea> quota_;
  std::shared_ptr<Block> root_block_;
};
