/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "eptree.h"

void EPTree::Init(uint32_t block_number) {
  RTree{block()}.Init(1, block_number);
}

bool EPTree::insert(const iterator::value_type& key_value) {
  auto it = find(key_value.key);
  if (it.nodes().back().node.insert(key_value)) {
    return true;
  }
  // Need to grow
  std::vector<FreeBlocksExtentInfo> allocated_extents;
  iterator::value_type key_val_to_add = key_value;
  for (auto& [node_level, _] : std::views::reverse(it.nodes())) {
    if (&node_level != &it.nodes().back().node && node_level.insert(key_val_to_add))
      break;
    auto depth = node_level.tree_header()->depth.value();
    // Where to split the level
    auto split_point = node_level.middle();
    key_type split_point_key = (*split_point).key();
    // Alloc new right side tree
    auto right_block_number = AllocBlockForTree(node_level.tree_header()->block_number.value(), allocated_extents);
    RTree new_right(allocator_->LoadAllocatorBlock(right_block_number, /*new_block=*/true));
    RTree new_left{node_level.block()};
    if (depth == tree_header()->depth.value()) {
      // This is the root split it to two new trees
      if (depth == 3) {
        // can't grow anymore
        return false;
      }
      // We need a new left side too
      auto left_block_number = AllocBlockForTree(node_level.tree_header()->block_number.value(), allocated_extents);
      new_left = {allocator_->LoadAllocatorBlock(left_block_number, /*new_block=*/true)};
      new_right.Init(depth, right_block_number);
      new_left.Init(depth, left_block_number);
      node_level.split(new_left, new_right, split_point);
      // Reset the root
      node_level.Init(depth + 1, node_level.tree_header()->block_number.value());
      node_level.insert({0, left_block_number});
      node_level.insert({split_point_key, right_block_number});
    } else {
      new_right.Init(depth, right_block_number);
      node_level.split(new_right, split_point);
    }
    [[maybe_unused]] bool inserted;
    if (key_val_to_add.key >= split_point_key) {
      inserted = new_right.insert(key_val_to_add);
    } else {
      inserted = new_left.insert(key_val_to_add);
    }
    assert(inserted);
    key_val_to_add = iterator::value_type{split_point_key, right_block_number};
  }
  for (auto extent : allocated_extents)
    allocator_->RemoveFreeBlocksExtent(extent);
  return true;
}

bool EPTree::insert(const RTree::iterator& it_start, const RTree::iterator& it_end) {
  for (const auto& val : std::ranges::subrange(it_start, it_end)) {
    if (!insert(val))
      return false;
  }
  return true;
}

void EPTree::erase(iterator& pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
  // Erase from each node
  for (auto& [node_level, node_it] : std::views::reverse(pos.nodes())) {
    node_level.erase(node_it);
    if (!node_level.empty()) {
      break;
    }
    if (node_level.header() == &tree_header()->current_tree) {
      // This is root, tree is empty.
      mutable_tree_header()->depth = 1;
    } else {
      // node is empty, delete parent too
      blocks_to_delete.push_back({node_level.tree_header()->block_number.value(), 1});
    }
  }
}

bool EPTree::erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
  auto it = find(key, true);
  if (it.is_end())
    return false;
  erase(it, blocks_to_delete);
  return true;
}

uint32_t EPTree::AllocBlockForTree(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated) {
  auto block_number = allocator_->AllocFreeBlockFromCache();
  if (block_number)
    return block_number;
  return allocator_->FindSmallestFreeBlockExtent(near, allocated);
}

EPTree::iterator EPTree::begin() const {
  std::vector<iterator::node_info> nodes;
  nodes.reserve(tree_header()->depth.value());
  assert(tree_header()->depth.value() >= 1);
  for (int i = 0; i < tree_header()->depth.value(); i++) {
    assert(i == 0 || !nodes.back().iterator.is_end());
    iterator::node_info node{i == 0 ? block() : allocator_->LoadAllocatorBlock((*nodes.back().iterator).value())};
    node.iterator = node.node.begin();
    nodes.push_back(std::move(node));
  }
  return {allocator_, std::move(nodes)};
}

EPTree::iterator EPTree::end() const {
  std::vector<iterator::node_info> nodes;
  nodes.reserve(tree_header()->depth.value());
  assert(tree_header()->depth.value() >= 1);
  for (int i = 0; i < tree_header()->depth.value(); i++) {
    assert(i == 0 || !nodes.back().iterator.is_begin());
    iterator::node_info node{i == 0 ? block() : allocator_->LoadAllocatorBlock((*--nodes.back().iterator).value())};
    node.iterator = node.node.end();
    nodes.push_back(std::move(node));
  }
  return {allocator_, std::move(nodes)};
}

EPTree::iterator EPTree::find(key_type key, bool exact_match) const {
  std::vector<iterator::node_info> nodes;
  nodes.reserve(tree_header()->depth.value());
  for (int i = 0; i < tree_header()->depth.value(); i++) {
    assert(i == 0 || !nodes.back().iterator.is_end());
    iterator::node_info node{i == 0 ? block() : allocator_->LoadAllocatorBlock((*nodes.back().iterator).value())};
    node.iterator = node.node.find(key, exact_match && i + 1 == tree_header()->depth.value());
    nodes.push_back(std::move(node));
  }
  return {allocator_, std::move(nodes)};
}
