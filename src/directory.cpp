/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory.h"

#include <numeric>
#include <utility>

#include "file.h"
#include "quota_area.h"
#include "structs.h"

Directory::Directory(std::string name,
                     AttributesRef attributes,
                     std::shared_ptr<QuotaArea> quota,
                     std::shared_ptr<Block> block)
    : WfsItem(std::move(name), std::move(attributes)), quota_(std::move(quota)), block_(std::move(block)) {}

std::expected<std::shared_ptr<WfsItem>, WfsError> Directory::GetObject(const std::string& name) const {
  try {
    auto it = find(name);
    if (it.is_end()) {
      return std::unexpected(WfsError::kItemNotFound);
    }
    return (*it).item;
  } catch (WfsException e) {
    return std::unexpected(e.error());
  }
}

std::expected<std::shared_ptr<Directory>, WfsError> Directory::GetDirectory(const std::string& name) const {
  auto obj = GetObject(name);
  if (!obj.has_value())
    return std::unexpected(obj.error());
  if (!(*obj)->is_directory()) {
    // Not a directory
    return std::unexpected(kNotDirectory);
  }
  return std::dynamic_pointer_cast<Directory>(*obj);
}

std::expected<std::shared_ptr<File>, WfsError> Directory::GetFile(const std::string& name) const {
  auto obj = GetObject(name);
  if (!obj.has_value())
    return std::unexpected(obj.error());
  if (!(*obj)->is_file()) {
    // Not a file
    return std::unexpected(kNotFile);
  }
  return std::dynamic_pointer_cast<File>(*obj);
}

Directory::iterator Directory::begin() const {
  auto current_block = block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node->begin();
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node->begin();
  assert(!leaf.iterator.is_end());
  return {quota_, std::move(parents), std::move(leaf)};
}

Directory::iterator Directory::end() const {
  auto current_block = block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node->end();
    assert(!parents.back().iterator.is_begin());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*--parents.back().iterator).value));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node->end();
  return {quota_, std::move(parents), std::move(leaf)};
}

Directory::iterator Directory::find(std::string_view key, bool exact_match) const {
  if (size() == 0)
    return end();
  auto current_block = block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node->find(key, /*exact_match=*/false);
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node->find(key, exact_match);
  return {quota_, std::move(parents), std::move(leaf)};
}

size_t Directory::CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const {
  if (block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
      MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE) {
    return DirectoryLeafTree{std::move(block)}.size();
  } else {
    DirectoryParentTree tree{std::move(block)};
    return std::accumulate(tree.begin(), tree.end(), size_t{0}, [&](auto acc, const auto& node) {
      return acc + CalcSizeOfDirectoryBlock(throw_if_error(quota_->LoadMetadataBlock(node.value)));
    });
  }
};
