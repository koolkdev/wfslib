/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>
#include <memory>
#include <vector>

#include "free_blocks_allocator.h"
#include "rtree.h"

class EPTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;
  using value_type = RTree::iterator::value_type;

  using reference = RTree::iterator::reference;

  using node_info = node_iterator_info<RTree>;

  EPTreeIterator() = default;

  EPTreeIterator(FreeBlocksAllocator* allocator, std::vector<node_info> nodes)
      : allocator_(allocator), nodes_(std::move(nodes)) {}

  reference operator*() const { return *nodes_.back().iterator; }

  EPTreeIterator& operator++();
  EPTreeIterator& operator--();
  EPTreeIterator operator++(int);
  EPTreeIterator operator--(int);

  bool operator==(const EPTreeIterator& other) const { return nodes_.back().iterator == other.nodes_.back().iterator; }

  std::vector<node_info>& nodes() { return nodes_; };
  const std::vector<node_info>& nodes() const { return nodes_; };

  bool is_begin() const {
    return std::ranges::all_of(nodes_, [](const node_info& node) { return node.iterator.is_begin(); });
  }
  bool is_end() const { return nodes_.back().iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;

  std::vector<node_info> nodes_;
};
static_assert(std::bidirectional_iterator<EPTreeIterator>);

class EPTree : public EPTreeBlock {
 public:
  using iterator = EPTreeIterator;

  EPTree(FreeBlocksAllocator* allocator) : EPTreeBlock(allocator->root_block()), allocator_(allocator) {}

  void Init(uint32_t block_number);

  iterator begin() const;
  iterator end() const;

  iterator find(key_type key, bool exact_match = true) const;

  bool insert(const iterator::value_type& key_value);
  bool insert(const RTree::iterator& it_start, const RTree::iterator& it_end);
  void erase(const iterator& pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);
  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);

 private:
  uint32_t AllocBlockForTree(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated);

  iterator begin_impl() const;
  iterator end_impl() const;
  iterator find_impl(key_type key, bool exact_match = true) const;

  FreeBlocksAllocator* allocator_;
};
static_assert(std::ranges::bidirectional_range<EPTree>);
