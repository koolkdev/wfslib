/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <iterator>
#include <memory>
#include <ranges>

#include "block.h"
#include "tree_utils.h"

struct node_ref {
  Block* block;
  uint16_t offset;
  uint16_t extra_info;

  template <is_node_details T>
  const T* get() const {
    return block->template get_object<T>(offset);
  }
  template <is_node_details T>
  T* get_mutable() const {
    return block->template get_mutable_object<T>(offset);
  }
  auto operator<=>(const node_ref& other) const {
    if (const auto res = block <=> other.block; res != 0)
      return res;
    return offset <=> other.offset;
  }
  bool operator==(const node_ref& other) const { return block == other.block && offset == other.offset; }
};

struct node_item_ref_base : node_ref {
  size_t index;
  auto operator<=>(const node_item_ref_base& other) const {
    if (const auto res = node_ref::operator<=>(other); res != 0)
      return res;
    return index <=> other.index;
  }
  bool operator==(const node_item_ref_base& other) const { return node_ref::operator==(other) && index == other.index; }
};

template <is_node_details T>
struct node_key_ref : protected node_item_ref_base {
  operator key_type() const {
    assert(index < node_values_size(*block->get_object<T>(offset)));
    return node_get_full_key(*block->get_object<T>(offset), index);
  }

  node_key_ref& operator=(const node_key_ref& other) {
    *this = static_cast<key_type>(other);
    return *this;
  }

  node_key_ref& operator=(key_type val) {
    node_set_full_key(*block->get_mutable_object<T>(offset), index, val);
    return *this;
  }

  auto operator<=>(const node_key_ref& other) const {
    return static_cast<key_type>(*this) <=> static_cast<key_type>(other);
  }
  bool operator==(const node_key_ref& other) const {
    return static_cast<key_type>(*this) == static_cast<key_type>(other);
  }
};

template <is_node_details T>
struct node_value_ref : protected node_item_ref_base {
  using value_type = typename node_value_type<T>::type;
  operator value_type() const {
    assert(index < node_values_size(*block->get_object<T>(offset)));
    return node_get_value(*block->get_object<T>(offset), index);
  }

  node_value_ref& operator=(const node_value_ref& other) {
    *this = static_cast<value_type>(other);
    return *this;
  }

  node_value_ref& operator=(value_type val) {
    node_set_value(*block->get_mutable_object<T>(offset), index, val);
    return *this;
  }

  auto operator<=>(const node_value_ref& other) const {
    return static_cast<value_type>(*this) <=> static_cast<value_type>(other);
  }
  bool operator==(const node_value_ref& other) const {
    return static_cast<value_type>(*this) == static_cast<value_type>(other);
  }
};

template <typename extra_info_type>
struct extra_info_ref : protected node_item_ref_base {
  operator extra_info_type() const { return extra_info; }
};

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
    node_key_ref<T> key;
    node_value_ref<T> value;
  };

  node_item_ref() = default;
  node_item_ref(const node_item_ref& other) = default;
  node_item_ref(node_item_ref&& other) = default;

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
auto operator<=>(key_type key, const node_item<T>& node_value) {
  return key <=> node_value;
}
template <is_node_details T>
auto operator==(key_type key, const node_item<T>& node_value) {
  return key == node_value;
}

template <is_node_details T>
class PTreeNodeConstIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = node_item<T>;

  using ref_type = node_item_ref<T>;
  static_assert(sizeof(ref_type) == sizeof(node_item_ref_base));

  using const_reference = const ref_type;
  using const_pointer = const ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  PTreeNodeConstIterator() = default;
  PTreeNodeConstIterator(node_ref node, size_t index)
      : PTreeNodeConstIterator(node_item_ref_base{node.block, node.offset, node.extra_info, index}) {}
  PTreeNodeConstIterator(node_item_ref_base node_item) : node_item_(node_item) {
    assert(node_item_.index <= node_values_capacity<T>::value);
  }

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return reinterpret_cast<pointer>(&node_item_); }

  PTreeNodeConstIterator& operator++() {
    ++node_item_.index;
    return *this;
  }

  PTreeNodeConstIterator& operator--() {
    --node_item_.index;
    return *this;
  }

  PTreeNodeConstIterator operator++(int) {
    PTreeNodeConstIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  PTreeNodeConstIterator operator--(int) {
    PTreeNodeConstIterator tmp(*this);
    --(*this);
    return tmp;
  }
  PTreeNodeConstIterator& operator+=(difference_type n) {
    node_item_.index += n;
    assert(node_item_.index <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeConstIterator& operator-=(difference_type n) {
    node_item_.index -= n;
    assert(node_item_.index <= node_values_capacity<T>::value);
    return *this;
  }

  PTreeNodeConstIterator operator+(difference_type n) const {
    PTreeNodeConstIterator tmp(*this);
    tmp += n;
    return tmp;
  }

  PTreeNodeConstIterator operator-(difference_type n) const {
    PTreeNodeConstIterator tmp(*this);
    tmp -= n;
    return tmp;
  }

  difference_type operator-(const PTreeNodeConstIterator& other) const {
    assert(node_item_.get<T>() == other.node_item_.template get<T>());
    return static_cast<difference_type>(node_item_.index) - static_cast<difference_type>(other.node_item_.index);
  }

  auto operator<=>(const PTreeNodeConstIterator& other) const {
    if (const auto res = node_item_.get<T>() <=> other.node_item_.template get<T>(); res != 0)
      return res;
    return node_item_.index <=> other.node_item_.index;
  }

  bool operator==(const PTreeNodeConstIterator& other) const { return node_item_ == other.node_item_; }

  const ref_type operator[](difference_type n) const { return *(*this + n); }

  bool is_begin() const { return node_item_.index == 0; }
  bool is_end() const {
    return node_item_.index == node_values_capacity<T>::value ||
           (node_item_.index > 0 && node_get_full_key(*node_item_.get<T>(), node_item_.index) == 0);
  }

 private:
  node_item_ref_base node_item_;
};

template <is_node_details T>
class PTreeNodeIterator : public PTreeNodeConstIterator<T> {
 public:
  using base = PTreeNodeConstIterator<T>;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = node_item<T>;

  using ref_type = node_item_ref<T>;

  using reference = ref_type;
  using pointer = ref_type*;

  PTreeNodeIterator() = default;
  PTreeNodeIterator(node_ref node, size_t index) : base(node, index) {}
  PTreeNodeIterator(node_item_ref_base node_item) : base(node_item) {}

  reference operator*() const { return *operator->(); }
  pointer operator->() const& { return const_cast<pointer>(base::operator->()); }

  PTreeNodeIterator& operator++() {
    base::operator++();
    return *this;
  }

  PTreeNodeIterator& operator--() {
    base::operator--();
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
    base::operator+=(n);
    return *this;
  }
  PTreeNodeIterator& operator-=(difference_type n) {
    base::operator-=(n);
    return *this;
  }
  PTreeNodeIterator operator+(difference_type n) const {
    PTreeNodeIterator tmp(*this);
    tmp += n;
    return tmp;
    ;
  }
  PTreeNodeIterator operator-(difference_type n) const {
    PTreeNodeIterator tmp(*this);
    tmp -= n;
    return tmp;
  }
  difference_type operator-(const PTreeNodeIterator& other) const { return base::operator-(other); }
  difference_type operator-(const PTreeNodeConstIterator<T>& other) const { return base::operator-(other); }

  reference operator[](difference_type n) const { return const_cast<pointer>(base::operator[](n)); }
};

template <is_node_details T>
PTreeNodeConstIterator<T> operator+(typename PTreeNodeConstIterator<T>::difference_type n,
                                    const PTreeNodeConstIterator<T>& iter) {
  return iter + n;
}

template <is_node_details T>
PTreeNodeIterator<T> operator+(typename PTreeNodeIterator<T>::difference_type n, const PTreeNodeIterator<T>& iter) {
  return iter + n;
}

template <is_node_details T>
PTreeNodeConstIterator<T>::difference_type operator-(const PTreeNodeConstIterator<T>& a,
                                                     const PTreeNodeIterator<T>& b) {
  return a.operator-(b);
}

template <is_node_details T>
  requires std::random_access_iterator<PTreeNodeIterator<T>> && std::random_access_iterator<PTreeNodeConstIterator<T>>
class PTreeNode {
 public:
  using iterator = PTreeNodeIterator<T>;
  using const_iterator = PTreeNodeConstIterator<T>;
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  PTreeNode(node_ref node) : PTreeNode(node, node_values_size(*node.get<T>())) {}
  PTreeNode(node_ref node, size_t size) : node_(node), size_(size) {}

  size_t size() const { return size_; }
  iterator begin() { return iterator(node_, 0); }
  iterator end() { return iterator(node_, size_); }
  const_iterator begin() const { return const_iterator(node_, 0); }
  const_iterator end() const { return const_iterator(node_, size_); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  reverse_iterator rbegin() { return {end()}; }
  reverse_iterator rend() { return {begin()}; }
  const_reverse_iterator rbegin() const { return {end()}; }
  const_reverse_iterator rend() const { return {begin()}; }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  bool full() const { return size_ == node_values_capacity<T>::value; }
  bool empty() const { return size_ == 0; }

  iterator find(key_type key, bool exact_match) {
    auto it = std::upper_bound(begin(), end(), key);
    if (it != begin())
      --it;
    if (exact_match && (it == end() || it->key != key))
      return end();
    return it;
  }

  iterator insert(const const_iterator& pos, const typename iterator::value_type& key_val) {
    if (size_ >= node_values_capacity<T>::value)
      return end();
    assert(cbegin() <= pos && pos <= cend());
    iterator res = begin() + (pos - cbegin());
    std::copy_backward<const_iterator, iterator>(res, cend(), end() + 1);
    *res = key_val;
    size_++;
    return res;
  }

  template <typename Range>
  iterator insert(const const_iterator& pos, Range&& range) {
    return insert(pos, std::ranges::begin(range), std::ranges::end(range));
  }

  template <typename InputIt>
  iterator insert(const const_iterator& pos, const InputIt& start_it, const InputIt& end_it) {
    auto items = static_cast<typename iterator::difference_type>(std::distance(start_it, end_it));
    if (size_ + items > node_values_capacity<T>::value)
      return end();
    assert(cbegin() <= pos && pos <= cend());
    iterator res = begin() + (pos - cbegin());
    std::copy_backward<const_iterator, iterator>(res, cend(), end() + items);
    std::copy(start_it, end_it, res);
    size_ += items;
    return res;
  }

  iterator erase(const const_iterator& pos) {
    assert(cbegin() <= pos && pos < cend());
    auto new_end = end() - 1;
    iterator res = begin() + (pos - cbegin());
    std::copy(pos + 1, cend(), res);
    std::fill(new_end, end(), 0);
    --size_;
    return res;
  }

  iterator erase(const const_iterator& it_start, const const_iterator& it_end) {
    assert(cbegin() <= it_start && it_start <= cend());
    assert(cbegin() <= it_end && it_end <= cend());
    assert(it_start <= it_end);
    iterator res = begin() + (it_start - cbegin());
    auto new_end = res + (cend() - it_end);
    std::copy(it_end, cend(), res);
    std::fill(new_end, end(), 0);
    size_ -= it_end - it_start;
    return res;
  }

  void clear(bool full = false) {
    std::fill(begin(), full ? (begin() + node_values_capacity<T>::value) : end(), 0);
    size_ = 0;
  }

  bool operator==(const PTreeNode& other) const { return node_ == other.node_; }

  const T* node() { return node_.get<T>(); }

 private:
  node_ref node_;
  size_t size_;
};
