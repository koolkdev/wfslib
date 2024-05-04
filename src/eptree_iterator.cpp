/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "eptree_iterator.h"

#include "free_blocks_allocator.h"

EPTreeIterator& EPTreeIterator::operator++() {
  assert(!is_end());
  auto rnode = nodes_.rbegin();
  while ((++rnode->iterator).is_end()) {
    if (++rnode == nodes_.rend()) {
      while (--rnode != nodes_.rbegin())
        --rnode->iterator;
      return *this;  // end
    }
  }
  uint32_t node_block_number = (*rnode->iterator).value;
  for (auto node = rnode.base(); node != nodes_.end(); ++node) {
    *node = {allocator_->LoadAllocatorBlock(node_block_number)};
    node->iterator = node->node->begin();
    node_block_number = (*node->iterator).value;
  }
  return *this;
}

EPTreeIterator& EPTreeIterator::operator--() {
  assert(!is_begin());
  auto rnode = nodes_.rbegin();
  for (; rnode->iterator.is_begin(); rnode++) {
    if (rnode == nodes_.rend())
      return *this;  // begin
  }
  uint32_t node_block_number = (*--rnode->iterator).value;
  for (auto node = rnode.base(); node != nodes_.end(); ++node) {
    *node = {allocator_->LoadAllocatorBlock(node_block_number)};
    node->iterator = node->node->end();
    node_block_number = (*--node->iterator).value;
  }
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
