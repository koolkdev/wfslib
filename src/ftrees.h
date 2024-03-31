/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <numeric>
#include <ranges>
#include <type_traits>

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
};
static_assert(sizeof(free_blocks_extent_ref) == sizeof(node_item_ref<FTreeLeaf_details>));

template <typename ftree_info_type>
class FTreesConstIteratorBase {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = int;

  using value_type = free_blocks_extent_ref;
  using ref_type = free_blocks_extent_ref;

  using const_reference = const ref_type;
  using const_pointer = const ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  using ftree_info = ftree_info_type;

  FTreesConstIteratorBase() = default;
  FTreesConstIteratorBase(std::array<ftree_info, kSizeBucketsCount> ftrees, size_t index)
      : ftrees_(std::move(ftrees)), index_(index) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return reinterpret_cast<pointer>(ftrees_[index_].iterator.operator->()); }

  FTreesConstIteratorBase& operator++() {
    assert(!is_end());
    ++ftrees_[index_].iterator;
    index_ = find_next_extent_index(ftrees_);
    return *this;
  }

  FTreesConstIteratorBase operator++(int) {
    FTreesConstIteratorBase tmp(*this);
    ++(*this);
    return tmp;
  }

  bool operator==(const FTreesConstIteratorBase& other) const {
    return index() == other.index() && ftrees_[index()].iterator == other.ftrees_[other.index()].iterator;
  }

  bool is_begin() const {
    return std::ranges::all_of(ftrees_, [](const ftree_info& ftree) { return ftree.iterator.is_begin(); });
  }
  bool is_end() const { return ftrees_[index()].iterator.is_end(); }

  template <typename Range, typename Compare>
  static auto find_extent(Range& range, Compare comp) {
    if constexpr (is_reverse_iterator_info<FTree, ftree_info_type>) {
      return std::ranges::max_element(range, comp);
    } else {
      return std::ranges::min_element(range, comp);
    }
  }

  static size_t find_next_extent_index(const std::array<ftree_info, kSizeBucketsCount>& ftrees) {
    auto iterated_ftrees =
        ftrees | std::views::filter([](const ftree_info& ftree) { return !ftree.iterator.is_end(); });
    auto res = find_extent(iterated_ftrees,
                           [](const ftree_info& a, const ftree_info& b) { return a.iterator->key < b.iterator->key; });
    return res != std::ranges::end(iterated_ftrees) ? res->node->index() : 0;
  }

 protected:
  const std::array<ftree_info, kSizeBucketsCount>& ftrees() const { return ftrees_; }
  size_t index() const { return index_; }

 private:
  std::array<ftree_info, kSizeBucketsCount> ftrees_;
  size_t index_{0};
};

template <typename ftree_info_type>
class FTreesIteratorBase : public FTreesConstIteratorBase<ftree_info_type> {
 public:
  using base = FTreesConstIteratorBase<ftree_info_type>;

  using iterator_category = std::forward_iterator_tag;
  using difference_type = int;

  using ref_type = base::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  using ftree_info = ftree_info_type;

  FTreesIteratorBase() = default;
  FTreesIteratorBase(std::array<ftree_info, kSizeBucketsCount> ftrees, size_t index) : base(std::move(ftrees), index) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return const_cast<pointer>(base::operator->()); }

  FTreesIteratorBase& operator++() {
    base::operator++();
    return *this;
  }

  FTreesIteratorBase operator++(int) {
    FTreesIteratorBase tmp(*this);
    ++(*this);
    return tmp;
  }

  bool operator==(const FTreesIteratorBase& other) const { return base::operator==(other); }
};

using FTreesForwardConstIterator = FTreesConstIteratorBase<node_iterator_info<FTree>>;
using FTreesBackwardConstIterator = FTreesConstIteratorBase<node_reverse_iterator_info<FTree>>;
using FTreesForwardIterator = FTreesIteratorBase<node_iterator_info<FTree>>;
using FTreesBackwardIterator = FTreesIteratorBase<node_reverse_iterator_info<FTree>>;
static_assert(std::forward_iterator<FTreesForwardConstIterator>);
static_assert(std::forward_iterator<FTreesBackwardConstIterator>);
static_assert(std::forward_iterator<FTreesForwardIterator>);
static_assert(std::forward_iterator<FTreesBackwardIterator>);

class FTrees {
 public:
  using iterator = FTreesForwardIterator;
  using const_iterator = FTreesForwardConstIterator;
  using reverse_iterator = FTreesBackwardIterator;
  using const_reverse_iterator = FTreesBackwardConstIterator;

  FTrees(std::shared_ptr<MetadataBlock> block)
      : ftrees_(CreateFTreeArray(std::move(block), std::make_index_sequence<kSizeBucketsCount>{})) {}

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

  auto cbegin() const { return begin(); }
  auto cend() const { return end(); }

  reverse_iterator rbegin() { return rbegin_impl(); }
  reverse_iterator rend() { return rend_impl(); }
  const_reverse_iterator rbegin() const { return rbegin_impl(); }
  const_reverse_iterator rend() const { return rend_impl(); }

  auto find(key_type key) { return find_impl(key); }
  auto rfind(key_type key) { return rfind_impl(key); }
  auto find(key_type key) const { return find_impl(key); }
  auto rfind(key_type key) const { return rfind_impl(key); }

  void split(FTrees& left, FTrees& right, key_type& split_point_key);

  std::array<FTree, kSizeBucketsCount>& ftrees() { return ftrees_; }

 private:
  template <std::size_t... Is>
  static std::array<FTree, kSizeBucketsCount> CreateFTreeArray(std::shared_ptr<MetadataBlock> block,
                                                               std::index_sequence<Is...>) {
    return {{{block, Is}...}};
  }

  iterator begin_impl() const;
  reverse_iterator rbegin_impl() const;
  iterator end_impl() const;
  reverse_iterator rend_impl() const;
  iterator find_impl(key_type key) const;
  reverse_iterator rfind_impl(key_type key) const;

  std::array<FTree, kSizeBucketsCount> ftrees_;
};
