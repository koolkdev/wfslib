/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "free_blocks_tree_iterator.h"

class FreeBlocksTree {
 public:
  using iterator = FreeBlocksTreeIterator;

  FreeBlocksTree(FreeBlocksAllocator* allocator) : allocator_(allocator) {}

  iterator begin() const;
  iterator end() const;

  iterator find(key_type key, bool exact_match = true) const;

 private:
  FreeBlocksAllocator* allocator_;
};
static_assert(std::ranges::bidirectional_range<FreeBlocksTree>);
