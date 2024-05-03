/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>

#include "tree_utils.h"

template <is_node_details T>
struct node_item {
  key_type key;
  typename node_value_type<T>::type value;

  auto operator<=>(const node_item& other) const {
    if (const auto res = key <=> other.key; res != 0)
      return res;
    return value <=> other.value;
  }
  bool operator==(const node_item& other) const { return key == other.key && value == other.value; }
  auto operator<=>(key_type key) const { return this->key <=> key; }
  bool operator==(key_type key) const { return this->key == key; }
};

template <is_node_details T>
struct node_item_ref {
  union {
    node_item_ref_base _base;
    node_key_ref<T> key;
    node_value_ref<T> value;
  };

  node_item_ref& operator=(const node_item_ref& other) {
    key = other.key;
    value = other.value;
    return *this;
  }

  node_item_ref& operator=(const node_item<T>& val) {
    key = val.key;
    value = val.value;
    return *this;
  }

  node_item_ref& operator=(key_type val) {
    key = val;
    return *this;
  }

  operator node_item<T>() const { return {key, value}; }

  auto operator<=>(const node_item_ref& other) const {
    return static_cast<node_item<T>>(*this) <=> static_cast<node_item<T>>(other);
  }
  bool operator==(const node_item_ref& other) const {
    return static_cast<node_item<T>>(*this) == static_cast<node_item<T>>(other);
  }
  auto operator<=>(key_type other_key) const { return static_cast<key_type>(this->key) <=> other_key; }
  bool operator==(key_type other_key) const { return static_cast<key_type>(this->key) == other_key; }
};

template <is_node_details T>
class PTreeNodeIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = node_item<T>;

  using ref_type = node_item_ref<T>;

  using reference = ref_type;

  PTreeNodeIterator() = default;
  PTreeNodeIterator(node_ref node, size_t index) : PTreeNodeIterator(node_item_ref_base{node, index}) {}
  PTreeNodeIterator(node_item_ref_base node_item) : node_item_(node_item) {
    assert(node_item_.index <= node_values_capacity<T>::value);
  }

  reference operator*() const { return {node_item_}; }

  PTreeNodeIterator& operator++() {
    ++node_item_.index;
    return *this;
  }

  PTreeNodeIterator& operator--() {
    --node_item_.index;
    return *this;
  }

  PTreeNodeIterator operator++(int) {
    PTreeNodeIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  PTreeNodeIterator operator--(int) {
    PTreeNodeIterator tmp(*this);
    --(*this);
    return tmp;
  }
  PTreeNodeIterator& operator+=(difference_type n) {
    node_item_.index += n;
    assert(node_item_.index <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeIterator& operator-=(difference_type n) {
    node_item_.index -= n;
    assert(node_item_.index <= node_values_capacity<T>::value);
    return *this;
  }

  PTreeNodeIterator operator+(difference_type n) const {
    PTreeNodeIterator tmp(*this);
    tmp += n;
    return tmp;
  }

  PTreeNodeIterator operator-(difference_type n) const {
    PTreeNodeIterator tmp(*this);
    tmp -= n;
    return tmp;
  }

  difference_type operator-(const PTreeNodeIterator& other) const {
    assert(node_item_.get<T>() == other.node_item_.template get<T>());
    return static_cast<difference_type>(node_item_.index) - static_cast<difference_type>(other.node_item_.index);
  }

  auto operator<=>(const PTreeNodeIterator& other) const {
    if (const auto res = node_item_.get<T>() <=> other.node_item_.template get<T>(); res != 0)
      return res;
    return node_item_.index <=> other.node_item_.index;
  }

  bool operator==(const PTreeNodeIterator& other) const { return node_item_ == other.node_item_; }

  reference operator[](difference_type n) const { return *(*this + n); }

  bool is_begin() const { return node_item_.index == 0; }
  bool is_end() const {
    return node_item_.index == node_values_capacity<T>::value ||
           (node_item_.index > 0 && node_get_full_key(*node_item_.get<T>(), node_item_.index) == 0);
  }

 private:
  node_item_ref_base node_item_;
};

template <is_node_details T>
PTreeNodeIterator<T> operator+(typename PTreeNodeIterator<T>::difference_type n, const PTreeNodeIterator<T>& iter) {
  return iter + n;
}
