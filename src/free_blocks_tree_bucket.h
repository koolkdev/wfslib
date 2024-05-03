/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <vector>

#include "free_blocks_tree_bucket_iterator.h"

class FreeBlocksTreeBucket {
 public:
  using iterator = FreeBlocksTreeBucketIterator;

  FreeBlocksTreeBucket(FreeBlocksAllocator* allocator, size_t block_size_index)
      : allocator_(allocator), block_size_index_(block_size_index) {}

  iterator begin() const;
  iterator end() const;

  iterator find(key_type key, bool exact_match = true) const;

  bool insert(FTree::iterator::value_type key_val);
  bool insert(iterator& pos, FTree::iterator::value_type key_val);

  void erase(iterator pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);
  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);

 private:
  iterator find_for_insert(key_type key) const;

  FreeBlocksAllocator* allocator_;

  size_t block_size_index_;
};
static_assert(std::ranges::bidirectional_range<FreeBlocksTreeBucket>);
