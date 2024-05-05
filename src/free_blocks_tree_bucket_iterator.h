/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>

#include "eptree.h"
#include "ftrees.h"
#include "tree_utils.h"

class FreeBlocksTreeBucketIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = FTrees::iterator::value_type;
  using ref_type = FTrees::iterator::ref_type;

  using reference = FTrees::iterator::reference;

  using eptree_node_info = node_iterator_info<EPTree>;
  using ftree_node_info = node_iterator_info<FTree>;

  FreeBlocksTreeBucketIterator() = default;
  FreeBlocksTreeBucketIterator(FreeBlocksAllocator* allocator,
                               size_t block_size_index,
                               eptree_node_info eptree,
                               ftree_node_info ftree)
      : allocator_(allocator),
        block_size_index_(block_size_index),
        eptree_(std::move(eptree)),
        ftree_(std::move(ftree)) {}

  reference operator*() const { return {*ftree_.iterator, block_size_index_}; }

  FreeBlocksTreeBucketIterator& operator++();
  FreeBlocksTreeBucketIterator& operator--();
  FreeBlocksTreeBucketIterator operator++(int);
  FreeBlocksTreeBucketIterator operator--(int);

  bool operator==(const FreeBlocksTreeBucketIterator& other) const { return ftree_.iterator == other.ftree_.iterator; }

  size_t block_size_index() const { return block_size_index_; }

  eptree_node_info& eptree() { return eptree_; }
  const eptree_node_info& eptree() const { return eptree_; }
  ftree_node_info& ftree() { return ftree_; }
  const ftree_node_info& ftree() const { return ftree_; }

  bool is_begin() const { return eptree_.iterator.is_begin() && ftree_.iterator.is_begin(); }
  bool is_end() const { return ftree_.iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;
  size_t block_size_index_;

  eptree_node_info eptree_;
  ftree_node_info ftree_;
};
static_assert(std::bidirectional_iterator<FreeBlocksTreeBucketIterator>);
