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

#include "free_blocks_allocator.h"
#include "ftree.h"

struct free_blocks_extent_ref {
  union {
    node_item_ref_base _base;
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
    return key == other.key && value == other.value && bucket_index == other.bucket_index;
  }
};

class FTreesIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;

  using value_type = free_blocks_extent_ref;
  using ref_type = free_blocks_extent_ref;

  using reference = ref_type;

  using ftree_info = node_iterator_info<FTree>;

  FTreesIterator() = default;
  FTreesIterator(std::array<ftree_info, kSizeBuckets.size()> ftrees, size_t index)
      : ftrees_(std::move(ftrees)), index_(index) {}

  reference operator*() const { return {(*ftrees_[index_].iterator)._base}; }

  FTreesIterator& operator++();
  FTreesIterator& operator--();

  FTreesIterator operator++(int);
  FTreesIterator operator--(int);

  bool operator==(const FTreesIterator& other) const {
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
      return max ^ ((*a.iterator).key > (*b.iterator).key);
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
static_assert(std::bidirectional_iterator<FTreesIterator>);
