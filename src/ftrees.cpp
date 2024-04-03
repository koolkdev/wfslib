/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "ftrees.h"

FTreesConstIterator& FTreesConstIterator::operator++() {
  assert(!is_end());
  if (is_forward_) {
    ++ftrees_[index_].iterator;
  } else {
    // Switch direction
    key_type key = (*this)->key;
    reverse_end_map_ &= ~(1 << index_);
    // todo: enumrate
    for (auto& ftree : ftrees_) {
      while (!(reverse_end_map_ & (1 << ftree.node->index())) && !ftree.iterator.is_end() &&
             (++ftree.iterator)->key < key) {
      }
    }
    is_forward_ = true;
    reverse_end_map_ = 0;
  }
  index_ = find_next_extent_index(ftrees_, /*max=*/false, reverse_end_map_);
  return *this;
}

FTreesConstIterator& FTreesConstIterator::operator--() {
  assert(!is_begin());
  if (!is_forward_) {
    if (!(reverse_end_map_ & (1 << index_))) {
      --ftrees_[index_].iterator;
    }
  } else {
    // Switch direction
    key_type key = is_end() ? std::numeric_limits<key_type>::max() : (*this)->key;
    for (auto& ftree : ftrees_) {
      if (ftree.iterator.is_begin()) {
        if (!ftree.iterator.is_end() && ftree.iterator->key >= key) {
          reverse_end_map_ |= 1 << ftree.node->index();
        }
      } else {
        while (!ftree.iterator.is_begin() && (--ftree.iterator)->key > key) {
        }
      }
    }
    is_forward_ = false;
  }
  index_ = find_next_extent_index(ftrees_, /*max=*/true, reverse_end_map_);
  if (ftrees_[index_].iterator.is_begin()) {
    reverse_end_map_ |= 1 << index_;
  }
  return *this;
}

FTreesConstIterator FTreesConstIterator::operator++(int) {
  FTreesConstIterator tmp(*this);
  ++(*this);
  return tmp;
}

FTreesConstIterator FTreesConstIterator::operator--(int) {
  FTreesConstIterator tmp(*this);
  --(*this);
  return tmp;
}

bool FTreesConstIterator::is_begin() const {
  if (is_forward_) {
    return std::ranges::all_of(ftrees_, [](const ftree_info& ftree) { return ftree.iterator.is_begin(); });
  } else {
    return std::ranges::all_of(ftrees_, [&](const ftree_info& ftree) {
      return reverse_end_map_ & (1 << ftree.node->index()) || ftree.iterator.is_end();
    });
  }
}

FTreesIterator& FTreesIterator::operator++() {
  FTreesConstIterator::operator++();
  return *this;
}

FTreesIterator& FTreesIterator::operator--() {
  FTreesConstIterator::operator--();
  return *this;
}

FTreesIterator FTreesIterator::operator++(int) {
  FTreesIterator tmp(*this);
  ++(*this);
  return tmp;
}

FTreesIterator FTreesIterator::operator--(int) {
  FTreesIterator tmp(*this);
  --(*this);
  return tmp;
}

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
  auto index = iterator::find_next_extent_index(ftrees_info, /*max=*/false);
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

FTrees::iterator FTrees::find_impl(key_type key, bool exact_match) const {
  std::array<typename iterator::ftree_info, kSizeBucketsCount> ftrees_info;
  std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                 [key](const FTree& cftree) -> typename iterator::ftree_info {
                   auto& ftree = const_cast<FTree&>(cftree);
                   return {ftree, ftree.find(key, false)};
                 });
  size_t index = 0;
  if (exact_match && !std::ranges::any_of(ftrees_info, [key](const iterator::ftree_info& ftree) {
        return !ftree.iterator.is_end() && ftree.iterator->key == key;
      })) {
    return end_impl();
  }
  auto before_keys = ftrees_info | std::views::filter([key](const iterator::ftree_info& ftree) {
                       return !ftree.iterator.is_end() && ftree.iterator->key <= key;
                     });
  if (!std::ranges::empty(before_keys)) {
    // take max key before or key
    index = iterator::find_next_extent_index(before_keys, /*max=*/true);
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
      index = iterator::find_next_extent_index(after_keys, /*max=*/false);
    }
  }
  return {std::move(ftrees_info), index};
}
