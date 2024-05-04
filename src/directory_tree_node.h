/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>

#include "directory_tree_node_iterator.h"

template <typename LeafValueType>
class DirectoryTreeNode {
 public:
  using iterator = DirectoryTreeNodeIterator<LeafValueType>;

  DirectoryTreeNode(dir_tree_node_ref node) : node_(node) {}

  size_t size() const { return node_->keys_count.value(); }

  iterator begin() const { return iterator(node_, 0); }
  iterator end() const { return iterator(node_, node_.get()->keys_count.value()); }

  bool empty() const { return size() == 0; }
  bool full(bool for_value) {
    return calc_node_size<LeafValueType>(node_->prefix_length.value(), node_->keys_count.value() + 1,
                                         for_value || node_.has_leaf_value());
  }

  iterator find(dir_tree_key_type key, bool exact_match) const {
    auto it = std::upper_bound(begin(), end(), key);
    if (it != begin())
      --it;
    if (exact_match && (it == end() || (*it).key != key))
      return end();
    return it;
  }

  iterator insert(const iterator& pos, const typename iterator::value_type& key_val) {
    assert(begin() <= pos && pos <= end());
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, end(), end() + 1);
    *res = key_val;
    ++node_->keys_count;
    assert(node_.node_size == node_.get_node_size(node_.get()));
    return res;
  }

  template <typename Range>
  iterator insert(const iterator& pos, Range&& range) {
    return insert(pos, std::ranges::begin(range), std::ranges::end(range));
  }

  template <typename InputIt>
  iterator insert(const iterator& pos, const InputIt& start_it, const InputIt& end_it) {
    auto items = static_cast<typename iterator::difference_type>(std::distance(start_it, end_it));
    assert(begin() <= pos && pos <= end());
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, end(), end() + items);
    std::copy(start_it, end_it, res);
    node_->keys_count += items;
    assert(node_.node_size == node_.get_node_size(node_.get()));
    return res;
  }

  iterator erase(const iterator& pos) {
    assert(begin() <= pos && pos < end());
    auto new_end = end() - 1;
    iterator res = begin() + (pos - begin());
    std::copy(pos + 1, end(), res);
    std::fill(new_end, end(), 0);
    --node_->keys_count;
    assert(node_.node_size == node_.get_node_size(node_.get()));
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
    node_->keys_count -= it_end - it_start;
    return res;
  }

  void clear() { node_->keys_count = 0; }

  bool operator==(const DirectoryTreeNode& other) const { return node_ == other.node_; }

  const DirectoryTreeNodeHeader* node() const { return node_.get(); }

  std::string_view prefix() const { return node_.prefix(); }

 private:
  dir_tree_node_ref node_;
};
