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

class FreeBlocksTreeConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = typename FTrees::iterator::value_type;
  using ref_type = typename FTrees::iterator::ref_type;

  using const_reference = value_type;
  using const_pointer = ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  using eptree_node_info = node_iterator_info<EPTree>;
  using ftrees_node_info = node_iterator_info<FTrees>;

  FreeBlocksTreeConstIterator() = default;
  FreeBlocksTreeConstIterator(FreeBlocksAllocator* allocator, eptree_node_info eptree, ftrees_node_info ftrees)
      : allocator_(allocator), eptree_(std::move(eptree)), ftrees_(std::move(ftrees)) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return reinterpret_cast<pointer>(ftrees_.iterator.operator->()); }

  FreeBlocksTreeConstIterator& operator++();
  FreeBlocksTreeConstIterator& operator--();

  FreeBlocksTreeConstIterator operator++(int);
  FreeBlocksTreeConstIterator operator--(int);

  bool operator==(const FreeBlocksTreeConstIterator& other) const { return ftrees_.iterator == other.ftrees_.iterator; }

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

class FreeBlocksTreeIterator : public FreeBlocksTreeConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = FreeBlocksTreeConstIterator::difference_type;

  using value_type = FreeBlocksTreeConstIterator::value_type;
  using ref_type = FreeBlocksTreeConstIterator::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  using eptree_node_info = FreeBlocksTreeConstIterator::eptree_node_info;
  using ftrees_node_info = FreeBlocksTreeConstIterator::ftrees_node_info;

  FreeBlocksTreeIterator() = default;
  FreeBlocksTreeIterator(FreeBlocksAllocator* allocator, eptree_node_info eptree, ftrees_node_info ftrees)
      : FreeBlocksTreeConstIterator(allocator, std::move(eptree), std::move(ftrees)) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return const_cast<pointer>(FreeBlocksTreeConstIterator::operator->()); }

  FreeBlocksTreeIterator& operator++();
  FreeBlocksTreeIterator& operator--();

  FreeBlocksTreeIterator operator++(int);
  FreeBlocksTreeIterator operator--(int);

  bool operator==(const FreeBlocksTreeIterator& other) const { return FreeBlocksTreeConstIterator::operator==(other); }
};

static_assert(std::bidirectional_iterator<FreeBlocksTreeConstIterator>);
static_assert(std::bidirectional_iterator<FreeBlocksTreeIterator>);

class FreeBlocksTree {
 public:
  using iterator = FreeBlocksTreeConstIterator;
  using const_iterator = FreeBlocksTreeConstIterator;
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  FreeBlocksTree(FreeBlocksAllocator* allocator) : allocator_(allocator) {}

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

 private:
  iterator begin_impl() const;
  iterator end_impl() const;
  iterator find_impl(key_type key, bool exact_match = true) const;

  FreeBlocksAllocator* allocator_;
};
