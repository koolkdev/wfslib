/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <algorithm>

#include "eptree.h"

template <>
PTreeNode<RTreeLeaf_details>::const_iterator split_point(
    const PTreeNode<RTreeLeaf_details>& node,
    const typename PTreeNode<RTreeLeaf_details>::const_iterator& pos,
    key_type& split_key) {
  assert(node.begin() <= pos && pos <= node.end());
  assert(node.full());
  auto res = pos;
  switch (pos - node.begin()) {
    case 0:
    case 1:
      res = node.begin() + 1;
      break;
    case 2:
      res = node.begin() + 2;
      break;
    case 3:
      return pos;
    case 4:
      res = node.begin() + 3;
      break;
  }
  split_key = res->key;
  return res;
}

void RTree::Init(uint8_t depth) {
  EPTreeBlock::Init();
  mutable_tree_header()->depth = depth;
  mutable_tree_header()->block_number = block_->BlockNumber();
}

EPTreeConstIterator& EPTreeConstIterator::operator++() {
  assert(!is_end());
  auto rnode = nodes_.rbegin();
  while ((++rnode->iterator).is_end()) {
    if (++rnode == nodes_.rend()) {
      while (--rnode != nodes_.rbegin())
        --rnode->iterator;
      return *this;  // end
    }
  }
  uint32_t node_block_number = rnode->iterator->value;
  for (auto node = rnode.base(); node != nodes_.end(); ++node) {
    *node = {allocator_->LoadAllocatorBlock(node_block_number)};
    node->iterator = node->node->begin();
    node_block_number = node->iterator->value;
  }
  return *this;
}

EPTreeConstIterator& EPTreeConstIterator::operator--() {
  assert(!is_begin());
  auto rnode = nodes_.rbegin();
  for (; rnode->iterator.is_begin(); rnode++) {
    if (rnode == nodes_.rend())
      return *this;  // begin
  }
  uint32_t node_block_number = (--rnode->iterator)->value;
  for (auto node = rnode.base(); node != nodes_.end(); ++node) {
    *node = {allocator_->LoadAllocatorBlock(node_block_number)};
    node->iterator = node->node->end();
    node_block_number = (--node->iterator)->value;
  }
  return *this;
}

EPTreeConstIterator EPTreeConstIterator::operator++(int) {
  EPTreeConstIterator tmp(*this);
  ++(*this);
  return tmp;
}

EPTreeConstIterator EPTreeConstIterator::operator--(int) {
  EPTreeConstIterator tmp(*this);
  --(*this);
  return tmp;
}

EPTreeIterator& EPTreeIterator::operator++() {
  base::operator++();
  return *this;
}

EPTreeIterator& EPTreeIterator::operator--() {
  base::operator--();
  return *this;
}

EPTreeIterator EPTreeIterator::operator++(int) {
  EPTreeIterator tmp(*this);
  ++(*this);
  return tmp;
}

EPTreeIterator EPTreeIterator::operator--(int) {
  EPTreeIterator tmp(*this);
  --(*this);
  return tmp;
}

void EPTree::Init() {
  EPTreeBlock::Init();
  RTree{block()}.Init(1);
}

bool EPTree::insert(const iterator::value_type& key_value) {
  auto it = find(key_value.key);
  if (it.nodes().back().node->insert(key_value)) {
    return true;
  }
  // Need to grow
  std::vector<FreeBlocksExtentInfo> allocated_extents;
  iterator::value_type key_val_to_add = key_value;
  for (auto& [node_level, _] : std::views::reverse(it.nodes())) {
    if (node_level != it.nodes().back().node && node_level->insert(key_val_to_add))
      break;
    auto depth = node_level->tree_header()->depth.value();
    // Where to split the level
    auto split_point = node_level->middle();
    key_type split_point_key = split_point->key;
    // Alloc new right side tree
    auto right_block_number = AllocBlockForTree(node_level->block()->BlockNumber(), allocated_extents);
    RTree new_right(allocator_->LoadAllocatorBlock(right_block_number, /*new_block=*/true));
    RTree new_left{node_level->block()};
    if (depth == tree_header()->depth.value()) {
      // This is the root split it to two new trees
      if (depth == 3) {
        // can't grow anymore
        return false;
      }
      // We need a new left side too
      auto left_block_number = AllocBlockForTree(node_level->block()->BlockNumber(), allocated_extents);
      new_left = {allocator_->LoadAllocatorBlock(left_block_number, /*new_block=*/true)};
      new_right.Init(depth);
      new_left.Init(depth);
      node_level->split(new_left, new_right, split_point);
      // Reset the root
      node_level->Init(depth + 1);
      node_level->insert({0, left_block_number});
      node_level->insert({split_point_key, right_block_number});
    } else {
      new_right.Init(depth);
      node_level->split(new_right, split_point);
    }
    bool inserted;
    if (key_val_to_add.key >= split_point_key) {
      inserted = new_right.insert(key_val_to_add);
    } else {
      inserted = new_left.insert(key_val_to_add);
    }
    std::ignore = inserted;  // for release debug
    assert(inserted);
    key_val_to_add = iterator::value_type{split_point_key, right_block_number};
  }
  for (auto extent : allocated_extents)
    allocator_->RemoveFreeBlocksExtent(extent);
  return true;
}

bool EPTree::insert(const RTree::const_iterator& it_start, const RTree::const_iterator& it_end) {
  for (const auto& val : std::ranges::subrange(it_start, it_end)) {
    if (!insert(val))
      return false;
  }
  return true;
}

void EPTree::erase(const const_iterator& pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
  // Erase from each node
  for (auto& [node_level, node_it] : std::views::reverse(pos.nodes())) {
    node_level->erase(node_it);
    if (!node_level->empty()) {
      break;
    }
    if (node_level->header() == &tree_header()->current_tree) {
      // This is root, tree is empty.
      mutable_tree_header()->depth = 1;
    } else {
      // node is empty, delete parent too
      blocks_to_delete.push_back({node_level->block()->BlockNumber(), 1});
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

EPTree::iterator EPTree::begin_impl() const {
  std::vector<iterator::node_info> nodes;
  nodes.reserve(tree_header()->depth.value());
  assert(tree_header()->depth.value() >= 1);
  for (int i = 0; i < tree_header()->depth.value(); i++) {
    assert(i == 0 || !nodes.back().iterator.is_end());
    iterator::node_info node{i == 0 ? block() : allocator_->LoadAllocatorBlock(nodes.back().iterator->value)};
    node.iterator = node.node->begin();
    nodes.push_back(std::move(node));
  }
  return {allocator_, std::move(nodes)};
}

EPTree::iterator EPTree::end_impl() const {
  std::vector<iterator::node_info> nodes;
  nodes.reserve(tree_header()->depth.value());
  assert(tree_header()->depth.value() >= 1);
  for (int i = 0; i < tree_header()->depth.value(); i++) {
    assert(i == 0 || !nodes.back().iterator.is_begin());
    iterator::node_info node{i == 0 ? block() : allocator_->LoadAllocatorBlock((--nodes.back().iterator)->value)};
    node.iterator = node.node->end();
    nodes.push_back(std::move(node));
  }
  return {allocator_, std::move(nodes)};
}

EPTree::iterator EPTree::find_impl(key_type key, bool exact_match) const {
  std::vector<iterator::node_info> nodes;
  nodes.reserve(tree_header()->depth.value());
  for (int i = 0; i < tree_header()->depth.value(); i++) {
    assert(i == 0 || !nodes.back().iterator.is_end());
    iterator::node_info node{i == 0 ? block() : allocator_->LoadAllocatorBlock((nodes.back().iterator)->value)};
    node.iterator = node.node->find(key, exact_match && i + 1 == tree_header()->depth.value());
    nodes.push_back(std::move(node));
  }
  return {allocator_, std::move(nodes)};
}

EPTree::reverse_iterator EPTree::rfind_impl(key_type key, bool exact_match) const {
  auto res = find_impl(key, exact_match);
  if (res.is_end())
    return reverse_iterator{res};
  else
    return reverse_iterator{++res};
}
