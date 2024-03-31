/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>
#include <vector>

#include "eptree.h"
#include "ftrees.h"
#include "tree_utils.h"

class FreeBlocksTreeBucketConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = FTrees::const_iterator::value_type;
  using ref_type = FTrees::const_iterator::ref_type;

  using const_reference = FTrees::const_iterator::const_reference;
  using const_pointer = FTrees::const_iterator::const_pointer;

  using reference = const_reference;
  using pointer = const_pointer;

  using eptree_node_info = node_iterator_info<EPTree>;
  using ftree_node_info = node_iterator_info<FTree>;

  FreeBlocksTreeBucketConstIterator() = default;
  FreeBlocksTreeBucketConstIterator(FreeBlocksAllocator* allocator,
                                    size_t block_size_index,
                                    eptree_node_info eptree,
                                    ftree_node_info ftree)
      : allocator_(allocator),
        block_size_index_(block_size_index),
        eptree_(std::move(eptree)),
        ftree_(std::move(ftree)) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return reinterpret_cast<pointer>(ftree_.iterator.operator->()); }

  FreeBlocksTreeBucketConstIterator& operator++();
  FreeBlocksTreeBucketConstIterator& operator--();
  FreeBlocksTreeBucketConstIterator operator++(int);
  FreeBlocksTreeBucketConstIterator operator--(int);

  bool operator==(const FreeBlocksTreeBucketConstIterator& other) const {
    return ftree_.iterator == other.ftree_.iterator;
  }

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
static_assert(std::bidirectional_iterator<FreeBlocksTreeBucketConstIterator>);

class FreeBlocksTreeBucketIterator : public FreeBlocksTreeBucketConstIterator {
 public:
  using base = FreeBlocksTreeBucketConstIterator;

  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = base::difference_type;

  using value_type = base::value_type;
  using ref_type = base::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  using eptree_node_info = base::eptree_node_info;
  using ftree_node_info = base::ftree_node_info;

  FreeBlocksTreeBucketIterator() = default;
  FreeBlocksTreeBucketIterator(FreeBlocksAllocator* allocator,
                               size_t block_size_index,
                               eptree_node_info eptree,
                               ftree_node_info ftree)
      : base(allocator, block_size_index, std::move(eptree), std::move(ftree)) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return const_cast<pointer>(base::operator->()); }

  FreeBlocksTreeBucketIterator& operator++();
  FreeBlocksTreeBucketIterator& operator--();
  FreeBlocksTreeBucketIterator operator++(int);
  FreeBlocksTreeBucketIterator operator--(int);

  bool operator==(const FreeBlocksTreeBucketIterator& other) const { return base::operator==(other); }
};
static_assert(std::bidirectional_iterator<FreeBlocksTreeBucketIterator>);

class FreeBlocksTreeBucket {
 public:
  using iterator = FreeBlocksTreeBucketIterator;
  using const_iterator = FreeBlocksTreeBucketConstIterator;
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  FreeBlocksTreeBucket(FreeBlocksAllocator* allocator, size_t block_size_index)
      : allocator_(allocator), block_size_index_(block_size_index) {}

  iterator begin() { return begin_impl(); }
  iterator end() { return end_impl(); }
  const_iterator begin() const { return begin_impl(); }
  const_iterator end() const { return end_impl(); }

  reverse_iterator rbegin() { return reverse_iterator{end()}; }
  reverse_iterator rend() { return reverse_iterator{begin()}; }
  const_reverse_iterator rbegin() const { return const_reverse_iterator{end()}; }
  const_reverse_iterator rend() const { return const_reverse_iterator{begin()}; }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  iterator find(key_type key, bool exact_match = true) { return find_impl(key, exact_match); }
  const_iterator find(key_type key, bool exact_match = true) const { return find_impl(key, exact_match); }

  bool insert(FTree::iterator::value_type key_val);
  bool insert(iterator& pos, FTree::iterator::value_type key_val);

  void erase(iterator pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);
  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);

 private:
  iterator begin_impl() const;
  iterator end_impl() const;
  iterator find_impl(key_type key, bool exact_match = true) const;

  iterator find_for_insert(key_type key) const;

  FreeBlocksAllocator* allocator_;

  size_t block_size_index_;
};
