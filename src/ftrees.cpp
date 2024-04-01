/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "ftrees.h"

void FTrees::split(FTrees& left, FTrees& right, key_type& split_point_key) {
  // Find the ftree with most items
  auto max_ftree_it = std::ranges::max_element(ftrees_, [](const FTree& a, const FTree& b) {
    return a.header()->items_count.value() < b.header()->items_count.value();
  });
  split_point_key = max_ftree_it->middle()->key;
  for (auto [old_ftree, left_ftree, right_ftree] : std::views::zip(ftrees_, left.ftrees_, right.ftrees_)) {
    auto pos = old_ftree.find(split_point_key, false);
    if (!pos.is_end() && pos->key < split_point_key)
      ++pos;
    old_ftree.split_compact(left_ftree, right_ftree, pos);
  }
}

void FTrees::Init() {
  ftrees_[0].Init();
}

FTrees::iterator FTrees::begin_impl() const {
  std::array<typename iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [](const FTree& cftree) -> typename iterator::ftree_info {
                   // We will convert the iterator back to const iterator if needed
                   auto& ftree = const_cast<FTree&>(cftree);
                   return {ftree, ftree.begin()};
                 });
  auto index = iterator::find_next_extent_index(ftrees_info);
  return {std::move(ftrees_info), index};
}

FTrees::reverse_iterator FTrees::rbegin_impl() const {
  std::array<typename reverse_iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [](const FTree& cftree) -> typename reverse_iterator::ftree_info {
                   auto& ftree = const_cast<FTree&>(cftree);
                   return {ftree, ftree.rbegin()};
                 });
  auto index = reverse_iterator::find_next_extent_index(ftrees_info);
  return {std::move(ftrees_info), index};
}

FTrees::iterator FTrees::end_impl() const {
  std::array<typename iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [](const FTree& cftree) -> typename iterator::ftree_info {
                   auto& ftree = const_cast<FTree&>(cftree);
                   return {ftree, ftree.end()};
                 });
  return {std::move(ftrees_info), 0};
}

FTrees::reverse_iterator FTrees::rend_impl() const {
  std::array<typename reverse_iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [](const FTree& cftree) -> typename reverse_iterator::ftree_info {
                   auto& ftree = const_cast<FTree&>(cftree);
                   return {ftree, ftree.rend()};
                 });
  return {std::move(ftrees_info), 0};
}

FTrees::iterator FTrees::find_impl(key_type key) const {
  std::array<typename iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [key](const FTree& cftree) -> typename iterator::ftree_info {
                   auto& ftree = const_cast<FTree&>(cftree);
                   return {ftree, ftree.find(key, false)};
                 });
  size_t index = 0;
  auto before_keys = ftrees_info | std::views::filter([key](const iterator::ftree_info& ftree) {
                       return !ftree.iterator.is_end() && ftree.iterator->key <= key;
                     });
  if (!std::ranges::empty(before_keys)) {
    // take max key before or key
    index = iterator::find_next_extent_index(before_keys, true);
    for (auto& before_it : before_keys) {
      // The rest of the before items should need to be after
      if (before_it.node->index() != index)
        ++before_it.iterator;
    }
  } else {
    auto after_keys = ftrees_info | std::views::filter([key](const iterator::ftree_info& ftree) {
                        return !ftree.iterator.is_end() && ftree.iterator->key > key;
                      });
    if (!std::ranges::empty(after_keys)) {
      index = iterator::find_next_extent_index(before_keys);
    }
  }
  return {std::move(ftrees_info), index};
}

FTrees::reverse_iterator FTrees::rfind_impl(key_type key) const {
  std::array<typename reverse_iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [key](const FTree& cftree) -> typename reverse_iterator::ftree_info {
                   auto& ftree = const_cast<FTree&>(cftree);
                   auto it = ftree.find(key, false);
                   if (!it.is_end())
                     ++it;
                   return {ftree, FTree::reverse_iterator{std::move(it)}};
                 });
  size_t index = 0;
  auto before_keys = ftrees_info | std::views::filter([key](const reverse_iterator::ftree_info& ftree) {
                       return !ftree.iterator.is_end() && ftree.iterator->key <= key;
                     });
  auto after_keys = ftrees_info | std::views::filter([key](const reverse_iterator::ftree_info& ftree) {
                      return !ftree.iterator.is_end() && ftree.iterator->key > key;
                    });
  if (!std::ranges::empty(before_keys)) {
    index = reverse_iterator::find_next_extent_index(before_keys);
  } else if (!std::ranges::empty(after_keys)) {
    index = reverse_iterator::find_next_extent_index(before_keys, true);
  }
  if (!std::ranges::empty(after_keys)) {
    for (auto& after_it : after_keys) {
      // The rest of the after items should need to be before
      if (after_it.node->index() != index)
        ++after_it.iterator;
    }
  }
  return {std::move(ftrees_info), index};
}
