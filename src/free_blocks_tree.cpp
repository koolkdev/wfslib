/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_tree.h"

FreeBlocksTree::iterator FreeBlocksTree::begin_impl() const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->begin();
  assert(!eptree.iterator.is_end());
  iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->begin();
  while (ftrees.iterator.is_end() && !eptree.iterator.is_end()) {
    ftrees = {{allocator_->LoadAllocatorBlock((++eptree.iterator)->value)}};
    ftrees.iterator = ftrees.node->begin();
  }
  return {allocator_, std::move(eptree), std::move(ftrees)};
}

FreeBlocksTree::reverse_iterator FreeBlocksTree::rbegin_impl() const {
  reverse_iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->rbegin();
  assert(!eptree.iterator.is_end());
  reverse_iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->rbegin();
  return {allocator_, std::move(eptree), std::move(ftrees)};
}

FreeBlocksTree::iterator FreeBlocksTree::end_impl() const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->end();
  assert(!eptree.iterator.is_begin());
  --eptree.iterator;  // EPTree size should always be >= 1
  iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->end();
  return {allocator_, std::move(eptree), std::move(ftrees)};
}

FreeBlocksTree::reverse_iterator FreeBlocksTree::rend_impl() const {
  reverse_iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->rend();
  assert(!eptree.iterator.is_begin());
  --eptree.iterator;  // EPTree size should always be >= 1
  reverse_iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->rend();
  while (ftrees.iterator.is_begin() && !eptree.iterator.is_begin()) {
    ftrees = {{allocator_->LoadAllocatorBlock((--eptree.iterator)->value)}};
    ftrees.iterator = ftrees.node->rend();
  }
  return {allocator_, std::move(eptree), std::move(ftrees)};
}

FreeBlocksTree::iterator FreeBlocksTree::find_impl(key_type key) const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->find(key, false);
  if (eptree.iterator.is_end())
    return end_impl();
  iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->find(key);
  if (!ftrees.iterator.is_end() && key >= ftrees.iterator->key) {
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }
  // If FTrees is empty or our key is smaller than the first key, go to previous node with value
  auto orig_eptree_it = eptree.iterator;
  // Go backward to search for value
  while (!eptree.iterator.is_begin()) {
    iterator::ftrees_node_info nftrees{{allocator_->LoadAllocatorBlock((--eptree.iterator)->value)}};
    nftrees.iterator = nftrees.node->find(key);
    if (!nftrees.iterator.is_end()) {
      ftrees = std::move(nftrees);
      return {allocator_, std::move(eptree), std::move(ftrees)};
    }
  }
  eptree.iterator = orig_eptree_it;
  // No smaller value, check if we had a value in the original ftree and return it.
  if (!ftrees.iterator.is_end()) {
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }
  // Go forward to search..
  while (!eptree.iterator.is_end()) {
    ftrees = {{allocator_->LoadAllocatorBlock((++eptree.iterator)->value)}};
    ftrees.iterator = ftrees.node->begin();
    if (!ftrees.iterator.is_end()) {
      // Found value
      break;
    }
  }
  return {allocator_, std::move(eptree), std::move(ftrees)};
}

FreeBlocksTree::reverse_iterator FreeBlocksTree::rfind_impl(key_type key) const {
  reverse_iterator::eptree_node_info eptree{{allocator_}};
  auto it = eptree.node->find(key, false);
  if (it.is_end())
    return rend_impl();
  eptree.iterator = EPTree::reverse_iterator{std::move(++it)};
  reverse_iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->rfind(key);
  return {allocator_, std::move(eptree), std::move(ftrees)};
}
