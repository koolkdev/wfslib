/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_tree_iterator.h"

FreeBlocksTreeIterator& FreeBlocksTreeIterator::operator++() {
  assert(!is_end());
  ++ftrees_.iterator;
  // support empty ftrees?
  while (ftrees_.iterator.is_end()) {
    if ((++eptree_.iterator).is_end()) {
      --eptree_.iterator;
      return *this;  // end
    }

    ftrees_ = {allocator_->LoadAllocatorBlock((*eptree_.iterator).value())};
    ftrees_.iterator = ftrees_.node->begin();
  }
  return *this;
}

FreeBlocksTreeIterator& FreeBlocksTreeIterator::operator--() {
  assert(!is_begin());
  // support empty ftrees?
  while (ftrees_.iterator.is_begin()) {
    if (eptree_.iterator.is_begin()) {
      return *this;  // begin
    }

    ftrees_ = {allocator_->LoadAllocatorBlock((*--eptree_.iterator).value())};
    ftrees_.iterator = ftrees_.node->end();
  }
  --ftrees_.iterator;
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
