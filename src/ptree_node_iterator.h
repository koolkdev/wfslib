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
  auto operator<=>(key_type key) const { return key <=> key; }
  bool operator==(key_type key) const { return key == key; }
};

template <is_node_details T>
struct node_item_ref {
 public:
  node_item_ref() = default;
  node_item_ref(node_ref<T> node, size_t index) : node(node), index(index) {}
  node_item_ref(const node_item_ref& other) = default;

  key_type key() const { return node_get_full_key(*node.get(), index); }
  typename node_value_type<T>::type value() const { return node_get_value(*node.get(), index); }
  void set_key(key_type key) { node_set_full_key(*node.get_mutable(), index, key); }
  void set_value(typename node_value_type<T>::type value) { node_set_value(*node.get_mutable(), index, value); }

  node_item_ref& operator=(const node_item_ref<T>& other) {
    set_key(other.key());
    set_value(other.value());
    return *this;
  }

  node_item_ref& operator=(const node_item<T>& val) {
    set_key(val.key);
    set_value(val.value);
    return *this;
  }

  node_item_ref& operator=(key_type val) {
    set_key(val);
    return *this;
  }

  operator node_item<T>() const { return {key(), value()}; }

  auto operator<=>(const node_item_ref& other) const {
    return static_cast<node_item<T>>(*this) <=> static_cast<node_item<T>>(other);
  }
  bool operator==(const node_item_ref& other) const {
    return static_cast<node_item<T>>(*this) == static_cast<node_item<T>>(other);
  }
  auto operator<=>(key_type other_key) const { return this->key() <=> other_key; }
  bool operator==(key_type other_key) const { return this->key() == other_key; }

 private:
  node_ref<T> node;
  size_t index;
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
  PTreeNodeIterator(node_ref<T> node, size_t index) : node_(node), index_(index) {}

  reference operator*() const {
    assert(index_ < node_values_capacity<T>::value);
    return {node_, index_};
  }

  PTreeNodeIterator& operator++() {
    ++index_;
    return *this;
  }

  PTreeNodeIterator& operator--() {
    --index_;
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
    index_ += n;
    assert(index_ <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeIterator& operator-=(difference_type n) {
    index_ -= n;
    assert(index_ <= node_values_capacity<T>::value);
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
    assert(node_.get() == other.node_.get());
    return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
  }

  auto operator<=>(const PTreeNodeIterator& other) const {
    if (const auto res = node_.get() <=> other.node_.get(); res != 0)
      return res;
    return index_ <=> other.index_;
  }

  bool operator==(const PTreeNodeIterator& other) const { return node_ == other.node_ && index_ == other.index_; }

  reference operator[](difference_type n) const { return *(*this + n); }

  bool is_begin() const { return index_ == 0; }
  bool is_end() const {
    return index_ == node_values_capacity<T>::value || (index_ > 0 && node_get_full_key(*node_.get(), index_) == 0);
  }

 private:
  node_ref<T> node_;
  size_t index_;
};

template <is_node_details T>
PTreeNodeIterator<T> operator+(typename PTreeNodeIterator<T>::difference_type n, const PTreeNodeIterator<T>& iter) {
  return iter + n;
}
