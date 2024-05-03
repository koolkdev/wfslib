/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>
#include <type_traits>

#include "eptree.h"
#include "ftrees.h"
#include "tree_utils.h"

class FreeBlocksTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = typename FTrees::iterator::value_type;
  using ref_type = typename FTrees::iterator::ref_type;

  using reference = value_type;

  using eptree_node_info = node_iterator_info<EPTree>;
  using ftrees_node_info = node_iterator_info<FTrees>;

  FreeBlocksTreeIterator() = default;
  FreeBlocksTreeIterator(FreeBlocksAllocator* allocator, eptree_node_info eptree, ftrees_node_info ftrees)
      : allocator_(allocator), eptree_(std::move(eptree)), ftrees_(std::move(ftrees)) {}

  reference operator*() const { return *ftrees_.iterator; }

  FreeBlocksTreeIterator& operator++();
  FreeBlocksTreeIterator& operator--();

  FreeBlocksTreeIterator operator++(int);
  FreeBlocksTreeIterator operator--(int);

  bool operator==(const FreeBlocksTreeIterator& other) const { return ftrees_.iterator == other.ftrees_.iterator; }

  eptree_node_info& eptree() { return eptree_; }
  const eptree_node_info& eptree() const { return eptree_; }
  ftrees_node_info& ftrees() { return ftrees_; }
  const ftrees_node_info& ftrees() const { return ftrees_; }

  bool is_begin() const { return eptree_.iterator.is_begin() && ftrees_.iterator.is_begin(); }
  bool is_end() const { return ftrees_.iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;

  eptree_node_info eptree_;
  ftrees_node_info ftrees_;
};
static_assert(std::bidirectional_iterator<FreeBlocksTreeIterator>);
