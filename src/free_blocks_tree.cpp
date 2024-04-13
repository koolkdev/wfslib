/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_tree.h"

FreeBlocksTreeConstIterator& FreeBlocksTreeConstIterator::operator++() {
  assert(!is_end());
  ++ftrees_.iterator;
  // support empty ftrees?
  while (ftrees_.iterator.is_end()) {
    if ((++eptree_.iterator).is_end()) {
      --eptree_.iterator;
      return *this;  // end
    }

    ftrees_ = {allocator_->LoadAllocatorBlock(eptree_.iterator->value)};
    ftrees_.iterator = ftrees_.node->begin();
  }
  return *this;
}

FreeBlocksTreeConstIterator& FreeBlocksTreeConstIterator::operator--() {
  assert(!is_begin());
  // support empty ftrees?
  while (ftrees_.iterator.is_begin()) {
    if (eptree_.iterator.is_begin()) {
      return *this;  // begin
    }

    ftrees_ = {allocator_->LoadAllocatorBlock((--eptree_.iterator)->value)};
    ftrees_.iterator = ftrees_.node->end();
  }
  --ftrees_.iterator;
  return *this;
}

FreeBlocksTreeConstIterator FreeBlocksTreeConstIterator::operator++(int) {
  FreeBlocksTreeConstIterator tmp(*this);
  ++(*this);
  return tmp;
}

FreeBlocksTreeConstIterator FreeBlocksTreeConstIterator::operator--(int) {
  FreeBlocksTreeConstIterator tmp(*this);
  --(*this);
  return tmp;
}

FreeBlocksTreeIterator& FreeBlocksTreeIterator::operator++() {
  FreeBlocksTreeConstIterator::operator++();
  return *this;
}

FreeBlocksTreeIterator& FreeBlocksTreeIterator::operator--() {
  FreeBlocksTreeConstIterator::operator--();
  return *this;
}

FreeBlocksTreeIterator FreeBlocksTreeIterator::operator++(int) {
  FreeBlocksTreeIterator tmp(*this);
  ++(*this);
  return tmp;
}

FreeBlocksTreeIterator FreeBlocksTreeIterator::operator--(int) {
  FreeBlocksTreeIterator tmp(*this);
  --(*this);
  return tmp;
}

FreeBlocksTree::iterator FreeBlocksTree::begin_impl() const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->begin();
  assert(!eptree.iterator.is_end());
  iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->begin();
  while (ftrees.iterator.is_end()) {
    if ((++eptree.iterator).is_end()) {
      // end
      --eptree.iterator;
      return {allocator_, std::move(eptree), std::move(ftrees)};
    }

    ftrees = {{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
    ftrees.iterator = ftrees.node->begin();
  }
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

FreeBlocksTree::iterator FreeBlocksTree::find_impl(key_type key, bool exact_match) const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node->find(key, false);
  if (eptree.iterator.is_end())
    return end_impl();
  iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
  ftrees.iterator = ftrees.node->find(key, exact_match);
  if (!ftrees.iterator.is_end() && key >= ftrees.iterator->key) {
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }
  // If FTrees is empty or our key is smaller than the first key, go to previous node with value
  if (exact_match) {
    return end_impl();
  }
  auto orig_eptree_it = eptree.iterator;
  // Go backward to search for value
  while (!eptree.iterator.is_begin()) {
    iterator::ftrees_node_info nftrees{{allocator_->LoadAllocatorBlock((--eptree.iterator)->value)}};
    nftrees.iterator = nftrees.node->find(key, exact_match);
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
  while (!(++eptree.iterator).is_end()) {
    ftrees = {{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
    ftrees.iterator = ftrees.node->begin();
    if (!ftrees.iterator.is_end()) {
      // Found value
      return {allocator_, std::move(eptree), std::move(ftrees)};
    }
  }
  --eptree.iterator;
  return {allocator_, std::move(eptree), std::move(ftrees)};
}
