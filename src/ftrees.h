/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <array>
#include <numeric>
#include <ranges>

#include "free_blocks_allocator.h"
#include "ftrees_iterator.h"

class FTrees {
 public:
  using iterator = FTreesIterator;

  FTrees() = default;
  FTrees(std::shared_ptr<Block> block)
      : ftrees_(CreateFTreeArray(std::move(block), std::make_index_sequence<kSizeBuckets.size()>{})) {}

  size_t size() const {
    return std::accumulate(ftrees_.begin(), ftrees_.end(), size_t{0},
                           [](auto acc, const FTree& ftree) { return acc + ftree.size(); });
  }

  bool empty() const { return size() == 0; }

  iterator begin() const;
  iterator end() const;

  iterator find(key_type key, bool exact_match = true) const;

  void split(FTrees& left, FTrees& right, key_type& split_point_key);

  void Init();

  std::array<FTree, kSizeBuckets.size()>& ftrees() { return ftrees_; }

 private:
  template <std::size_t... Is>
  static std::array<FTree, kSizeBuckets.size()> CreateFTreeArray(std::shared_ptr<Block> block,
                                                                 std::index_sequence<Is...>) {
    return {{{block, Is}...}};
  }

  std::array<FTree, kSizeBuckets.size()> ftrees_;
};
static_assert(std::ranges::bidirectional_range<FTrees>);
