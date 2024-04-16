/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <iterator>
#include <memory>
#include <numeric>
#include <ranges>
#include <type_traits>
#include <variant>

#include "free_blocks_allocator.h"
#include "ftree.h"

struct free_blocks_extent_ref {
  union {
    node_key_ref<FTreeLeaf_details> key;
    node_value_ref<FTreeLeaf_details> value;
    const extra_info_ref<size_t> bucket_index;
  };

  uint32_t block_number() const { return key; }
  uint32_t blocks_count() const {
    return (static_cast<uint32_t>(static_cast<nibble>(value)) + 1) << kSizeBuckets[bucket_index];
  }
  uint32_t end_block_number() const { return block_number() + blocks_count(); }

  operator FreeBlocksExtentInfo() const { return {block_number(), blocks_count(), bucket_index}; }

  bool operator==(const free_blocks_extent_ref& other) const {
    uint32_t _key = key;
    std::ignore = _key;
    return key == other.key && value == other.value && bucket_index == other.bucket_index;
  }
};
static_assert(sizeof(free_blocks_extent_ref) == sizeof(node_item_ref<FTreeLeaf_details>));

class FTreesConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = free_blocks_extent_ref;
  using ref_type = free_blocks_extent_ref;

  using const_reference = const ref_type;
  using const_pointer = const ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  using ftree_info = node_iterator_info<FTree>;

  FTreesConstIterator() = default;
  FTreesConstIterator(std::array<ftree_info, kSizeBuckets.size()> ftrees, size_t index)
      : ftrees_(std::move(ftrees)), index_(index) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return reinterpret_cast<pointer>(ftrees_[index_].iterator.operator->()); }

  FTreesConstIterator& operator++();
  FTreesConstIterator& operator--();

  FTreesConstIterator operator++(int);
  FTreesConstIterator operator--(int);

  bool operator==(const FTreesConstIterator& other) const {
    return index_ == other.index_ && ftrees_[index_].iterator == other.ftrees_[other.index_].iterator;
  }

  bool is_begin() const;
  bool is_end() const { return ftrees_[index_].iterator.is_end(); }

  template <typename Range>
  static size_t find_next_extent_index(Range& ftrees, bool max, std::bitset<kSizeBuckets.size()> reverse_end = {}) {
    auto iterated_ftrees = ftrees | std::views::filter([&reverse_end](const ftree_info& ftree) {
                             return !ftree.iterator.is_end() && !reverse_end.test(ftree.node->index());
                           });
    auto res = std::ranges::max_element(iterated_ftrees, [max](const ftree_info& a, const ftree_info& b) {
      return max ^ (a.iterator->key > b.iterator->key);
    });
    return res != std::ranges::end(iterated_ftrees) ? res->node->index() : 0;
  }

  const std::array<ftree_info, kSizeBuckets.size()>& ftrees() const { return ftrees_; }
  size_t index() const { return index_; }

 private:
  std::array<ftree_info, kSizeBuckets.size()> ftrees_;
  size_t index_{0};

  bool is_forward_{true};
  std::bitset<kSizeBuckets.size()> reverse_end_;
};

class FTreesIterator : public FTreesConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using ref_type = FTreesConstIterator::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  FTreesIterator() = default;
  FTreesIterator(std::array<ftree_info, kSizeBuckets.size()> ftrees, size_t index)
      : FTreesConstIterator(std::move(ftrees), index) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return const_cast<pointer>(FTreesConstIterator::operator->()); }

  FTreesIterator& operator++();
  FTreesIterator& operator--();

  FTreesIterator operator++(int);
  FTreesIterator operator--(int);

  bool operator==(const FTreesIterator& other) const { return FTreesConstIterator::operator==(other); }
};

static_assert(std::bidirectional_iterator<FTreesConstIterator>);
static_assert(std::bidirectional_iterator<FTreesIterator>);

class FTrees {
 public:
  using iterator = FTreesIterator;
  using const_iterator = FTreesConstIterator;
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  FTrees(std::shared_ptr<Block> block)
      : ftrees_(CreateFTreeArray(std::move(block), std::make_index_sequence<kSizeBuckets.size()>{})) {}

  size_t size() const {
    // TODO: llvm fold support
    // return *std::ranges::fold_right_last(
    // ftrees_ | std::views::transform([](const FTree& ftree) { return ftree.size(); }), std::plus<>());
    return std::accumulate(ftrees_.begin(), ftrees_.end(), size_t{0},
                           [](auto acc, const FTree& ftree) { return acc + ftree.size(); });
  }

  bool empty() const { return size() == 0; }

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

  void split(FTrees& left, FTrees& right, key_type& split_point_key);

  void Init();

  std::array<FTree, kSizeBuckets.size()>& ftrees() { return ftrees_; }

 private:
  template <std::size_t... Is>
  static std::array<FTree, kSizeBuckets.size()> CreateFTreeArray(std::shared_ptr<Block> block,
                                                                 std::index_sequence<Is...>) {
    return {{{block, Is}...}};
  }

  iterator begin_impl() const;
  iterator end_impl() const;
  iterator find_impl(key_type key, bool exact_match = true) const;

  std::array<FTree, kSizeBuckets.size()> ftrees_;
};
