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

template <typename eptree_node_info_type, typename ftrees_node_info_type>
class FreeBlocksTreeConstIteratorBase {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = typename FTrees::iterator::value_type;
  using ref_type = typename FTrees::iterator::ref_type;

  using const_reference = value_type;
  using const_pointer = ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  using eptree_node_info = eptree_node_info_type;
  using ftrees_node_info = ftrees_node_info_type;

  FreeBlocksTreeConstIteratorBase() = default;
  FreeBlocksTreeConstIteratorBase(FreeBlocksAllocator* allocator, eptree_node_info eptree, ftrees_node_info ftrees)
      : allocator_(allocator), eptree_(std::move(eptree)), ftrees_(std::move(ftrees)) {}

  reference operator*() const { return *ftrees_.iterator; }
  pointer operator->() const { return ftrees_.iterator.operator->(); }

  FreeBlocksTreeConstIteratorBase& operator++() {
    assert(!is_end());
    ++ftrees_.iterator;
    // support empty ftrees?
    while (ftrees_.iterator.is_end()) {
      if ((++eptree_.iterator).is_end()) {
        --eptree_.iterator;
        return *this;  // end
      }

      ftrees_ = {allocator_->LoadAllocatorBlock(eptree_.iterator->value)};
      ftrees_.iterator = ftrees_.node->template tbegin<decltype(ftrees_node_info::iterator)>();
    }
    return *this;
  }

  FreeBlocksTreeConstIteratorBase operator++(int) {
    FreeBlocksTreeConstIteratorBase tmp(*this);
    ++(*this);
    return tmp;
  }

  bool operator==(const FreeBlocksTreeConstIteratorBase& other) const {
    return ftrees_.iterator == other.ftrees_.iterator;
  }

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

template <typename eptree_node_info_type, typename ftrees_node_info_type>
class FreeBlocksTreeIteratorBase
    : public FreeBlocksTreeConstIteratorBase<eptree_node_info_type, ftrees_node_info_type> {
 public:
  using base = FreeBlocksTreeConstIteratorBase<eptree_node_info_type, ftrees_node_info_type>;

  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = base::difference_type;
  using value_type = base::value_type;
  using ref_type = base::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  using eptree_node_info = eptree_node_info_type;
  using ftrees_node_info = ftrees_node_info_type;

  FreeBlocksTreeIteratorBase() = default;
  FreeBlocksTreeIteratorBase(FreeBlocksAllocator* allocator, eptree_node_info eptree, ftrees_node_info ftrees)
      : base(allocator, std::move(eptree), std::move(ftrees)) {}

  reference operator*() const { return *base::ftrees().iterator; }
  pointer operator->() const { return base::ftrees().iterator.operator->(); }

  FreeBlocksTreeIteratorBase& operator++() {
    base::operator++();
    return *this;
  }

  FreeBlocksTreeIteratorBase operator++(int) {
    FreeBlocksTreeIteratorBase tmp(*this);
    ++(*this);
    return tmp;
  }

  bool operator==(const FreeBlocksTreeIteratorBase& other) const { return base::operator==(other); }
};

using FreeBlocksTreeForwardIterator =
    FreeBlocksTreeIteratorBase<node_iterator_info<EPTree>, node_iterator_info<FTrees>>;
using FreeBlocksTreeBackwardIterator =
    FreeBlocksTreeIteratorBase<node_reverse_iterator_info<EPTree>, node_reverse_iterator_info<FTrees>>;
using FreeBlocksTreeConstForwardIterator =
    FreeBlocksTreeConstIteratorBase<node_iterator_info<EPTree>, node_iterator_info<FTrees>>;
using FreeBlocksTreeConstBackwardIterator =
    FreeBlocksTreeConstIteratorBase<node_reverse_iterator_info<EPTree>, node_reverse_iterator_info<FTrees>>;
static_assert(std::forward_iterator<FreeBlocksTreeForwardIterator>);
static_assert(std::forward_iterator<FreeBlocksTreeBackwardIterator>);
static_assert(std::forward_iterator<FreeBlocksTreeConstForwardIterator>);
static_assert(std::forward_iterator<FreeBlocksTreeConstBackwardIterator>);

class FreeBlocksTree {
 public:
  using iterator = FreeBlocksTreeForwardIterator;
  using const_iterator = FreeBlocksTreeConstForwardIterator;
  using reverse_iterator = FreeBlocksTreeBackwardIterator;
  using const_reverse_iterator = FreeBlocksTreeConstBackwardIterator;

  FreeBlocksTree(FreeBlocksAllocator* allocator) : allocator_(allocator) {}

  iterator begin() { return tbegin<iterator>(); }
  iterator end() { return tend<iterator>(); }
  const_iterator begin() const { return tbegin<iterator>(); }
  const_iterator end() const { return tend<iterator>(); }

  auto cbegin() const { return begin(); }
  auto cend() const { return end(); }

  reverse_iterator rbegin() { return tbegin<reverse_iterator>(); }
  reverse_iterator rend() { return tend<reverse_iterator>(); }
  const_reverse_iterator rbegin() const { return tbegin<reverse_iterator>(); }
  const_reverse_iterator rend() const { return tend<reverse_iterator>(); }

  auto find(key_type key) { return tfind<iterator>(key); }
  auto rfind(key_type key) { return tfind<reverse_iterator>(key); }
  auto find(key_type key) const { return tfind<iterator>(key); }
  auto rfind(key_type key) const { return tfind<reverse_iterator>(key); }

 private:
  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, reverse_iterator>
  Iterator tbegin() const {
    typename Iterator::eptree_node_info eptree{{allocator_}};
    if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
      eptree.iterator = eptree.node->rbegin();
    } else {
      eptree.iterator = eptree.node->begin();
    }
    assert(!eptree.iterator.is_end());
    typename Iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
    ftrees.iterator = ftrees.node->template tbegin<decltype(Iterator::ftrees_node_info::iterator)>();
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, reverse_iterator>
  Iterator tend() const {
    typename Iterator::eptree_node_info eptree{{allocator_}};
    if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
      eptree.iterator = eptree.node->rend();
    } else {
      eptree.iterator = eptree.node->end();
    }
    assert(!eptree.iterator.is_begin());
    --eptree.iterator;  // EPTree size should always be >= 1
    typename Iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
    ftrees.iterator = ftrees.node->template tend<decltype(Iterator::ftrees_node_info::iterator)>();
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, reverse_iterator>
  Iterator tfind(key_type key) const {
    typename Iterator::eptree_node_info eptree{{allocator_}};
    if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
      eptree.iterator = eptree.node->rfind(key, false);
    } else {
      eptree.iterator = eptree.node->find(key, false);
    }
    typename Iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock(eptree.iterator->value)}};
    ftrees.iterator = ftrees.node->template tfind<decltype(Iterator::ftrees_node_info::iterator)>(key);
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }
  FreeBlocksAllocator* allocator_;
};
