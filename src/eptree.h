/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <vector>

#include "eptree_iterator.h"
#include "free_blocks_allocator.h"

class EPTree : public EPTreeBlock {
 public:
  using iterator = EPTreeIterator;

  EPTree() = default;
  EPTree(FreeBlocksAllocator* allocator) : EPTreeBlock(allocator->root_block()), allocator_(allocator) {}

  void Init(uint32_t block_number);

  iterator begin() const;
  iterator end() const;

  iterator find(key_type key, bool exact_match = true) const;

  bool insert(const iterator::value_type& key_value);
  bool insert(const RTree::iterator& it_start, const RTree::iterator& it_end);
  void erase(iterator& pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);
  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);

 private:
  uint32_t AllocBlockForTree(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated);

  iterator begin_impl() const;
  iterator end_impl() const;
  iterator find_impl(key_type key, bool exact_match = true) const;

  FreeBlocksAllocator* allocator_;
};
static_assert(std::ranges::bidirectional_range<EPTree>);
