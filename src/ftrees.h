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
#include "ptree.h"
#include "structs.h"
#include "tree_nodes_allocator.h"

template <>
PTreeNode<FTreeLeaf_details>::const_iterator split_point(
    const PTreeNode<FTreeLeaf_details>& node,
    const typename PTreeNode<FTreeLeaf_details>::const_iterator& pos,
    key_type& split_key);

static_assert(sizeof(PTreeNode_details) == sizeof(FTreeLeaf_details));
using FTreesBlock = TreeNodesAllocator<FTreesBlockHeader, FTreesFooter, sizeof(PTreeNode_details)>;

class FTree : public PTree<PTreeNode_details, FTreeLeaf_details, FTreesBlock> {
 public:
  FTree(std::shared_ptr<MetadataBlock> block, size_t block_size_index)
      : PTree<PTreeNode_details, FTreeLeaf_details, FTreesBlock>(std::move(block)),
        block_size_index_(block_size_index) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->trees[block_size_index_]; }
  const PTreeHeader* header() const override { return &tree_header()->trees[block_size_index_]; }

  size_t index() const { return block_size_index_; }

 private:
  size_t block_size_index_;
};

class FreeBlocksExtent {
 public:
  FreeBlocksExtent() = default;
  FreeBlocksExtent(PTreeNodeIteratorValueRef<FTreeLeaf_details> key_value, size_t bucket_index)
      : key_value_(std::move(key_value)), bucket_index_(bucket_index) {}

  uint32_t block_number() const { return key_value_.key; }
  uint32_t blocks_count() const {
    return (static_cast<uint32_t>(static_cast<nibble>(key_value_.value)) + 1) << kSizeBuckets[bucket_index_];
  }
  uint32_t end_block_number() const { return block_number() + blocks_count(); }
  size_t bucket_index() const { return bucket_index_; }
  const PTreeNodeIteratorValue<FTreeLeaf_details> key_value() const { return key_value_; }

  operator FreeBlocksExtentInfo() const { return {block_number(), blocks_count(), bucket_index()}; }

 protected:
  PTreeNodeIteratorValueRef<FTreeLeaf_details> key_value_;
  size_t bucket_index_;
};

class FreeBlocksExtentRef : public FreeBlocksExtent {
 public:
  FreeBlocksExtentRef() = default;
  FreeBlocksExtentRef(PTreeNodeIteratorValueRef<FTreeLeaf_details> key_value, size_t bucket_index)
      : FreeBlocksExtent(std::move(key_value), bucket_index) {}

  PTreeNodeIteratorValueRef<FTreeLeaf_details> key_value() { return key_value_; }
};

template <typename ftree_info_type>
class FTreesConstIteratorBase {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = int;

  using value_type = FreeBlocksExtent;
  using ref_type = FreeBlocksExtentRef;

  using const_reference = value_type;
  using const_pointer = const ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  using ftree_info = ftree_info_type;

  FTreesConstIteratorBase() = default;
  FTreesConstIteratorBase(std::array<ftree_info, kSizeBucketsCount> ftrees, size_t index)
      : ftrees_(std::move(ftrees)), index_(index) {}

  reference operator*() const { return {*ftrees_[index_].iterator, index_}; }
  pointer operator->() const {
    const_cast<FTreesConstIteratorBase*>(this)->extent_ = {*ftrees_[index_].iterator, index_};
    return &extent_;
  }

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

  FreeBlocksExtentRef extent_;
};

template <typename ftree_info_type>
class FTreesIteratorBase : public FTreesConstIteratorBase<ftree_info_type> {
 public:
  using base = FTreesConstIteratorBase<ftree_info_type>;

  using iterator_category = std::forward_iterator_tag;
  using difference_type = int;

  using value_type = base::value_type;
  using ref_type = base::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  using ftree_info = ftree_info_type;

  FTreesIteratorBase() = default;
  FTreesIteratorBase(std::array<ftree_info, kSizeBucketsCount> ftrees, size_t index) : base(std::move(ftrees), index) {}

  reference operator*() const { return *base::operator->(); }
  pointer operator->() const { return const_cast<pointer>(base::operator->()); }

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

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, reverse_iterator>
  Iterator tbegin() const {
    std::array<typename Iterator::ftree_info, kSizeBucketsCount> ftrees_info;
    std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                   [](const FTree& cftree) -> typename Iterator::ftree_info {
                     // We will convert the iterator back to const iterator if needed
                     auto& ftree = const_cast<FTree&>(cftree);
                     if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
                       return {ftree, ftree.rbegin()};
                     } else {
                       return {ftree, ftree.begin()};
                     }
                   });
    auto index = Iterator::find_next_extent_index(ftrees_info);
    return {std::move(ftrees_info), index};
  }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, reverse_iterator>
  Iterator tend() const {
    std::array<typename Iterator::ftree_info, kSizeBucketsCount> ftrees_info;
    std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                   [](const FTree& cftree) -> typename Iterator::ftree_info {
                     auto& ftree = const_cast<FTree&>(cftree);
                     if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
                       return {ftree, ftree.rend()};
                     } else {
                       return {ftree, ftree.end()};
                     }
                   });
    return {std::move(ftrees_info), 0};
  }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, reverse_iterator>
  Iterator tfind(key_type key) const {
    std::array<typename Iterator::ftree_info, kSizeBucketsCount> ftrees_info;
    std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                   [key](const FTree& cftree) -> typename Iterator::ftree_info {
                     auto& ftree = const_cast<FTree&>(cftree);
                     auto it = ftree.find(key, false);
                     if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
                       return {ftree, FTree::reverse_iterator{std::move(it)}};
                     } else {
                       // We want the iterator to be AFTER the search point
                       if (!it.is_end() && it->key != key)
                         ++it;
                       return {ftree, std::move(it)};
                     }
                   });
    // Finding the minimum will give us the closest point above the key
    auto index = Iterator::find_next_extent_index(ftrees_info);
    return {std::move(ftrees_info), index};
  }

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

  void split(FTrees& left, FTrees& right, key_type& split_point_key);

  std::array<FTree, kSizeBucketsCount>& ftrees() { return ftrees_; }

 private:
  template <std::size_t... Is>
  static std::array<FTree, kSizeBucketsCount> CreateFTreeArray(std::shared_ptr<MetadataBlock> block,
                                                               std::index_sequence<Is...>) {
    return {{{block, Is}...}};
  }

  std::array<FTree, kSizeBucketsCount> ftrees_;
};
