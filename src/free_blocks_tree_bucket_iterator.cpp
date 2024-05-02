/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_tree_bucket_iterator.h"

FreeBlocksTreeBucketIterator& FreeBlocksTreeBucketIterator::operator++() {
  assert(!is_end());
  ++ftree_.iterator;
  while (ftree_.iterator.is_end()) {
    if ((++eptree_.iterator).is_end()) {
      --eptree_.iterator;
      return *this;  // end
    }

    ftree_ = {{allocator_->LoadAllocatorBlock((*eptree_.iterator).value), block_size_index_}};
    ftree_.iterator = ftree_.node->begin();
  }
  return *this;
}

FreeBlocksTreeBucketIterator& FreeBlocksTreeBucketIterator::operator--() {
  assert(!is_begin());
  while (ftree_.iterator.is_begin()) {
    if (eptree_.iterator.is_begin()) {
      return *this;  // begin
    }

    ftree_ = {{allocator_->LoadAllocatorBlock((*--eptree_.iterator).value), block_size_index_}};
    ftree_.iterator = ftree_.node->end();
  }
  --ftree_.iterator;
  return *this;
}

FreeBlocksTreeBucketIterator FreeBlocksTreeBucketIterator::operator++(int) {
  FreeBlocksTreeBucketIterator tmp(*this);
  ++(*this);
  return tmp;
}

FreeBlocksTreeBucketIterator FreeBlocksTreeBucketIterator::operator--(int) {
  FreeBlocksTreeBucketIterator tmp(*this);
  --(*this);
  return tmp;
}
