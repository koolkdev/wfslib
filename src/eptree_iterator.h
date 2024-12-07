/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>
#include <vector>

#include "rtree.h"

class FreeBlocksAllocator;

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
