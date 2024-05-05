/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <memory>
#include <ranges>

#include "ptree_node_iterator.h"
#include "tree_utils.h"

template <is_node_details T>
class PTreeNode {
 public:
  using iterator = PTreeNodeIterator<T>;
  static_assert(std::random_access_iterator<iterator>);

  using reference = typename iterator::reference;

  PTreeNode() = default;
  PTreeNode(node_ref<T> node) : node_(node) {}

  size_t size() const { return node_values_size(*node_.get()); }
  iterator begin() const { return iterator(node_, 0); }
  iterator end() const { return iterator(node_, size()); }

  bool full() const { return size() == node_values_capacity<T>::value; }

  iterator find(key_type key, bool exact_match) {
    auto it = std::upper_bound(begin(), end(), key);
    if (it != begin())
      --it;
    if (exact_match && (it == end() || (*it).key() != key))
      return end();
    return it;
  }

  iterator insert(const iterator& pos, const typename iterator::value_type& key_val) {
    assert(size() < node_values_capacity<T>::value);
    assert(begin() <= pos && pos <= end());
    auto old_end = end();
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, old_end, old_end + 1);
    *res = key_val;
    return res;
  }

  template <typename Range>
  iterator insert(const iterator& pos, Range&& range) {
    return insert(pos, std::ranges::begin(range), std::ranges::end(range));
  }

  template <typename InputIt>
  iterator insert(const iterator& pos, const InputIt& start_it, const InputIt& end_it) {
    auto items = static_cast<typename iterator::difference_type>(std::distance(start_it, end_it));
    assert(size() + items <= node_values_capacity<T>::value);
    assert(begin() <= pos && pos <= end());
    auto old_end = end();
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, old_end, old_end + items);
    std::copy(start_it, end_it, res);
    return res;
  }

  // Return true if the tree is empty after erasing
  bool erase(const iterator& pos) {
    assert(begin() <= pos && pos < end());
    auto old_end = end();
    auto new_end = old_end - 1;
    iterator res = begin() + (pos - begin());
    std::copy(pos + 1, old_end, res);
    std::fill(new_end, old_end, 0);
    return new_end == begin();
  }

  bool erase(const iterator& it_start, const iterator& it_end) {
    assert(begin() <= it_start && it_start <= end());
    assert(begin() <= it_end && it_end <= end());
    assert(it_start <= it_end);
    iterator res = begin() + (it_start - begin());
    auto old_end = end();
    auto new_end = res + (old_end - it_end);
    std::copy(it_end, old_end, res);
    std::fill(new_end, old_end, 0);
    return new_end == begin();
  }

  void clear() { std::fill(begin(), begin() + node_values_capacity<T>::value, 0); }

  bool operator==(const PTreeNode& other) const { return node_ == other.node_; }

  const T* node() { return node_.get(); }

 private:
  node_ref<T> node_;
};
