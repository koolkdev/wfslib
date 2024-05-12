/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <optional>

#include "directory_tree_node_iterator.h"

template <typename LeafValueType>
class DirectoryTreeNode {
 public:
  using iterator = DirectoryTreeNodeIterator<LeafValueType>;

  DirectoryTreeNode() = default;
  DirectoryTreeNode(dir_tree_node_ref<LeafValueType> node) : node_(node) {}

  bool has_leaf() const { return node_.has_leaf_value(); }
  std::optional<dir_tree_leaf_node_item_ref<LeafValueType>> leaf() const {
    if (!has_leaf())
      return std::nullopt;
    return dir_tree_leaf_node_item_ref<LeafValueType>{node_, 0};
  }

  bool set_leaf(LeafValueType value, bool check_size = true) {
    dir_tree_leaf_node_item_ref<LeafValueType> leaf_ref{node_, 0};
    if (!has_leaf()) {
      if (check_size && allocated_size() != calc_new_node_size(prefix().size(), size(), /*has_leaf_value=*/true)) {
        return false;
      }
      auto old_keys_values = std::ranges::to<std::vector<typename iterator::value_type>>(*this);
      node_->keys_count = 1;
      leaf_ref.set_key(0);
      insert(begin(), old_keys_values);
    }
    leaf_ref.set_value(value);
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  bool remove_leaf(bool check_size = true) {
    if (!has_leaf()) {
      assert(false);
      return true;
    }
    if (check_size && allocated_size() != calc_new_node_size(prefix().size(), size(), /*has_leaf_value=*/false)) {
      return false;
    }
    auto old_keys_values = std::ranges::to<std::vector<typename iterator::value_type>>(*this);
    clear();
    insert(begin(), old_keys_values, /*check_size=*/false);
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  size_t size() const { return node_->keys_count.value() - node_.has_leaf_value(); }

  iterator begin() const { return iterator(node_, node_.has_leaf_value()); }
  iterator end() const { return iterator(node_, node_.get()->keys_count.value()); }

  bool empty() const { return size() == 0; }

  iterator find(dir_tree_key_type key, bool exact_match) const {
    auto it = std::upper_bound(begin(), end(), key);
    if (it != begin())
      --it;
    if (exact_match && (it == end() || (*it).key() != key))
      return end();
    return it;
  }

  bool insert(const iterator& pos, const typename iterator::value_type& key_val, bool check_size = true) {
    if (check_size && allocated_size() != calc_new_node_size(prefix().size(), size() + has_leaf(), has_leaf())) {
      return false;
    }
    assert(begin() <= pos && pos <= end());
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, end(), end() + 1);
    *res = key_val;
    ++node_->keys_count;
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  template <typename Range>
  bool insert(const iterator& pos, Range&& range, bool check_size = true) {
    return insert(pos, std::ranges::begin(range), std::ranges::end(range), check_size);
  }

  template <typename InputIt>
  bool insert(const iterator& pos, const InputIt& start_it, const InputIt& end_it, bool check_size = true) {
    auto items = static_cast<typename iterator::difference_type>(std::distance(start_it, end_it));
    if (check_size && allocated_size() != calc_new_node_size(prefix().size(), size() + items, has_leaf())) {
      return false;
    }
    assert(begin() <= pos && pos <= end());
    iterator res = begin() + (pos - begin());
    std::copy_backward(res, end(), end() + items);
    std::copy(start_it, end_it, res);
    node_->keys_count += static_cast<uint8_t>(items);
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  bool erase(const iterator& pos, bool check_size = true) {
    if (check_size && allocated_size() != calc_new_node_size(prefix().size(), size() - 1, has_leaf())) {
      return false;
    }
    assert(begin() <= pos && pos < end());
    auto new_end = end() - 1;
    iterator res = begin() + (pos - begin());
    std::copy(pos + 1, end(), res);
    std::fill(new_end, end(), 0);
    --node_->keys_count;
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  bool erase(const iterator& it_start, const iterator& it_end, bool check_size = true) {
    auto items = static_cast<typename iterator::difference_type>(std::distance(it_start, it_end));
    if (check_size && allocated_size() != calc_new_node_size(prefix().size(), size() - items, has_leaf())) {
      return false;
    }
    assert(begin() <= it_start && it_start <= end());
    assert(begin() <= it_end && it_end <= end());
    assert(it_start <= it_end);
    iterator res = begin() + (it_start - begin());
    auto new_end = res + (end() - it_end);
    std::copy(it_end, end(), res);
    std::fill(new_end, end(), 0);
    node_->keys_count -= items;
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  void clear() { node_->keys_count = 0; }

  bool operator==(const DirectoryTreeNode& other) const { return node_ == other.node_; }

  const DirectoryTreeNodeHeader* node() const { return node_.get(); }
  size_t offset() const { return node_.offset; }
  size_t allocated_size() const { return node_.node_size; }

  std::string_view prefix() const { return node_.prefix(); }
  bool set_prefix(std::string_view prefix, bool check_size = true) {
    if (check_size && allocated_size() != calc_new_node_size(prefix.size(), size(), has_leaf())) {
      return false;
    }
    node_.get_mutable()->prefix_length = prefix.size();
    std::ranges::copy(prefix, node_.mutable_prefix().begin());
    auto old_leaf_value = leaf();
    auto old_keys_values = std::ranges::to<std::vector<typename iterator::value_type>>(*this);
    clear();
    if (old_leaf_value) {
      set_leaf(*old_leaf_value, /*check_size=*/false);
    }
    insert(begin(), old_keys_values, /*check_size=*/false);
    assert(!check_size || allocated_size() == node_.get_node_size(node_.get()));
    return true;
  }

  static size_t calc_new_node_size(size_t new_prefix_length, size_t new_childs_length, bool has_leaf) {
    return calc_node_size<LeafValueType>(static_cast<uint8_t>(new_prefix_length),
                                         static_cast<uint8_t>(new_childs_length + has_leaf), has_leaf);
  }

 private:
  dir_tree_node_ref<LeafValueType> node_;
};
