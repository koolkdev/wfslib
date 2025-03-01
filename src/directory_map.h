/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "directory_map_iterator.h"

template <typename T>
concept DirectoryTreeImpl = std::same_as<T, DirectoryLeafTree> || std::same_as<T, DirectoryParentTree>;

class DirectoryMap {
 public:
  using iterator = DirectoryMapIterator;

  DirectoryMap(std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> root_block);

  size_t size() const { return CalcSizeOfDirectoryBlock(root_block_); }

  iterator begin() const;
  iterator end() const;

  iterator find(std::string_view key) const;

  bool insert(std::string_view name, const EntryMetadata* metadata);
  bool erase(std::string_view name);

  void Init();

 private:
  template <DirectoryTreeImpl TreeType>
  bool split_tree(std::vector<iterator::parent_node_info>& parents, TreeType& tree, std::string_view for_key);
  Block::DataRef<EntryMetadata> alloc_metadata(iterator it, size_t log2_size);

  size_t CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const;

  std::shared_ptr<QuotaArea> quota_;
  std::shared_ptr<Block> root_block_;
};
