/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_map.h"

#include <numeric>
#include <utility>

#include "quota_area.h"
#include "structs.h"

DirectoryMap::DirectoryMap(std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> root_block)
    : quota_(std::move(quota)), root_block_(std::move(root_block)) {}

DirectoryMap::iterator DirectoryMap::begin() const {
  auto current_block = root_block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.begin();
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value()));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node.begin();
  return {quota_, std::move(parents), std::move(leaf)};
}

DirectoryMap::iterator DirectoryMap::end() const {
  auto current_block = root_block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.end();
    assert(!parents.back().iterator.is_begin());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*--parents.back().iterator).value()));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node.end();
  return {quota_, std::move(parents), std::move(leaf)};
}

DirectoryMap::iterator DirectoryMap::find(std::string_view key, bool exact_match) const {
  if (size() == 0)
    return end();
  auto current_block = root_block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.find(key, /*exact_match=*/false);
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value()));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node.find(key, exact_match);
  return {quota_, std::move(parents), std::move(leaf)};
}

size_t DirectoryMap::CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const {
  if (block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
      MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE) {
    return DirectoryLeafTree{std::move(block)}.size();
  } else {
    DirectoryParentTree tree{std::move(block)};
    return std::accumulate(tree.begin(), tree.end(), size_t{0}, [&](auto acc, const auto& node) {
      return acc + CalcSizeOfDirectoryBlock(throw_if_error(quota_->LoadMetadataBlock(node.value())));
    });
  }
};
