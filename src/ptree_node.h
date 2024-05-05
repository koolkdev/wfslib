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

  PTreeNode(node_ref<T> node) : PTreeNode(node, node_values_size(*node.get())) {}
  PTreeNode(node_ref<T> node, size_t size) : node_(node), size_(size) {}

  size_t size() const { return size_; }
  iterator begin() const { return iterator(node_, 0); }
  iterator end() const { return iterator(node_, size_); }

  bool full() const { return size_ == node_values_capacity<T>::value; }
  bool empty() const { return size_ == 0; }

  iterator find(key_type key, bool exact_match) {
    auto it = std::upper_bound(begin(), end(), key);
    if (it != begin())
      --it;
    if (exact_match && (it == end() || (*it).key() != key))
      return end();
    return it;
  }

  iterator insert(const iterator& pos, const typename iterator::value_type& key_val) {
    if (size_ >= node_values_capacity<T>::value)
      return end();
    assert(begin() <= pos && pos <= end());
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, end(), end() + 1);
    *res = key_val;
    size_++;
    return res;
  }

  template <typename Range>
  iterator insert(const iterator& pos, Range&& range) {
    return insert(pos, std::ranges::begin(range), std::ranges::end(range));
  }

  template <typename InputIt>
  iterator insert(const iterator& pos, const InputIt& start_it, const InputIt& end_it) {
    auto items = static_cast<typename iterator::difference_type>(std::distance(start_it, end_it));
    if (size_ + items > node_values_capacity<T>::value)
      return end();
    assert(begin() <= pos && pos <= end());
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, end(), end() + items);
    std::copy(start_it, end_it, res);
    size_ += items;
    return res;
  }

  iterator erase(const iterator& pos) {
    assert(begin() <= pos && pos < end());
    auto new_end = end() - 1;
    iterator res = begin() + (pos - begin());
    std::copy(pos + 1, end(), res);
    std::fill(new_end, end(), 0);
    --size_;
    return res;
  }

  iterator erase(const iterator& it_start, const iterator& it_end) {
    assert(begin() <= it_start && it_start <= end());
    assert(begin() <= it_end && it_end <= end());
    assert(it_start <= it_end);
    iterator res = begin() + (it_start - begin());
    auto new_end = res + (end() - it_end);
    std::copy(it_end, end(), res);
    std::fill(new_end, end(), 0);
    size_ -= it_end - it_start;
    return res;
  }

  void clear(bool full = false) {
    std::fill(begin(), full ? (begin() + node_values_capacity<T>::value) : end(), 0);
    size_ = 0;
  }

  bool operator==(const PTreeNode& other) const { return node_ == other.node_; }

  const T* node() { return node_.get(); }

 private:
  node_ref<T> node_;
  size_t size_;
};
