/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include "directory_map_iterator.h"
#include "entry.h"
#include "errors.h"

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
  std::expected<Entry::MetadataHandleRef, WfsError> metadata_handle(std::string_view name) const;

  bool insert(std::string_view name, const EntryMetadata* metadata);
  bool erase(std::string_view name);
  std::expected<Block::DataRef<EntryMetadata>, WfsError> replace_metadata(std::string_view name,
                                                                          const EntryMetadata* metadata);

  void Init();

 private:
  template <DirectoryTreeImpl TreeType>
  bool split_tree(std::vector<iterator::parent_node_info>& parents, TreeType& tree, std::string_view for_key);
  Block::DataRef<EntryMetadata> alloc_metadata(iterator it, size_t log2_size);
  Block::DataRef<EntryMetadata> realloc_metadata(iterator it, size_t log2_size);
  void update_metadata_handle(std::string_view name, Block::DataRef<EntryMetadata> metadata) const;

  size_t CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const;

  std::shared_ptr<QuotaArea> quota_;
  std::shared_ptr<Block> root_block_;
  mutable std::map<std::string, std::weak_ptr<Entry::MetadataHandle>, std::less<>> metadata_handles_;
};
