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

template <is_node_details T>
struct node_ref {
  std::shared_ptr<Block> block;
  uint16_t offset;

  const T* get() const { return block->template get_object<T>(offset); }
  T* get_mutable() const { return block->template get_mutable_object<T>(offset); }
  auto operator<=>(const node_ref& other) const { return get() <=> other.get(); }
  bool operator==(const node_ref& other) const { return get() == other.get(); }
};

template <is_node_details T>
class PTreeNodeConstIterator;

template <is_node_details T>
class PTreeNodeKeyRef {
 public:
  PTreeNodeKeyRef() = default;
  PTreeNodeKeyRef(node_ref<T> node, size_t index) : node(std::move(node)), index(index) {}

  operator key_type() const {
    assert(index < node_values_size(*node.get()));
    return node_get_full_key(*node.get(), index);
  }

  PTreeNodeKeyRef& operator=(key_type val) {
    node_set_full_key(*node.get_mutable(), index, val);
    return *this;
  }

 private:
  friend class PTreeNodeConstIterator<T>;

  node_ref<T> node;
  size_t index;
};

template <is_node_details T>
class PTreeNodeValueRef {
 public:
  PTreeNodeValueRef() = default;
  PTreeNodeValueRef(node_ref<T> node, size_t index) : node(std::move(node)), index(index) {}

  operator typename node_value_type<T>::type() const {
    assert(index < node_values_size(*node.get()));
    return node_get_value(*node.get(), index);
  }

  PTreeNodeValueRef& operator=(node_value_type<T>::type val) {
    node_set_value(*node.get_mutable(), index, val);
    return *this;
  }

 private:
  friend class PTreeNodeConstIterator<T>;

  node_ref<T> node;
  size_t index;
};

template <is_node_details T>
struct PTreeNodeIteratorValue {
  key_type key;
  typename node_value_type<T>::type value;

  auto operator<=>(const PTreeNodeIteratorValue& other) const {
    auto res = key <=> other.key;
    if (res != 0)
      return res;
    return value <=> other.value;
  }
  bool operator==(const PTreeNodeIteratorValue& other) const { return key == other.key && value == other.value; }
  auto operator<=>(key_type key) const { return this->key <=> key; }
  bool operator==(key_type key) const { return this->key == key; }
};

template <is_node_details T>
auto operator<=>(key_type key, const PTreeNodeIteratorValue<T>& node_value) {
  return key <=> node_value;
}
template <is_node_details T>
auto operator==(key_type key, const PTreeNodeIteratorValue<T>& node_value) {
  return key == node_value;
}

template <is_node_details T>
struct PTreeNodeIteratorValueRef {
  PTreeNodeKeyRef<T> key;
  PTreeNodeValueRef<T> value;

  PTreeNodeIteratorValueRef& operator=(const PTreeNodeIteratorValue<T>& val) {
    key = val.key;
    value = val.value;
    return *this;
  }

  PTreeNodeIteratorValueRef& operator=(key_type val) {
    key = val;
    return *this;
  }

  operator PTreeNodeIteratorValue<T>() const { return {key, value}; }

  auto operator<=>(const PTreeNodeIteratorValueRef& other) const {
    return static_cast<PTreeNodeIteratorValue<T>>(*this) <=> static_cast<PTreeNodeIteratorValue<T>>(other);
  }
  bool operator==(const PTreeNodeIteratorValueRef& other) const {
    return static_cast<PTreeNodeIteratorValue<T>>(*this) == static_cast<PTreeNodeIteratorValue<T>>(other);
  }
  auto operator<=>(key_type other_key) const { return static_cast<key_type>(this->key) <=> other_key; }
  bool operator==(key_type other_key) const { return static_cast<key_type>(this->key) == other_key; }
};

template <is_node_details T>
class PTreeNodeConstIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = PTreeNodeIteratorValue<T>;

  using ref_type = PTreeNodeIteratorValueRef<T>;

  using const_reference = value_type;
  using const_pointer = const ref_type*;

  using reference = const_reference;
  using pointer = const_pointer;

  PTreeNodeConstIterator() = default;
  PTreeNodeConstIterator(node_ref<T> node, size_t index) : value_{{node, index}, {node, index}} {
    assert(index <= node_values_capacity<T>::value);
  }

  reference operator*() const { return value_; }

  pointer operator->() const { return &value_; }

  PTreeNodeConstIterator& operator++() {
    ++value_.key.index;
    ++value_.value.index;
    return *this;
  }

  PTreeNodeConstIterator& operator--() {
    --value_.key.index;
    --value_.value.index;
    return *this;
  }

  PTreeNodeConstIterator operator++(int) { return PTreeNodeConstIterator(node(), index() + 1); }

  PTreeNodeConstIterator operator--(int) { return PTreeNodeConstIterator(node(), index() - 1); }

  PTreeNodeConstIterator& operator+=(difference_type n) {
    value_.key.index += n;
    value_.value.index += n;
    assert(index() <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeConstIterator& operator-=(difference_type n) {
    value_.key.index -= n;
    value_.value.index -= n;
    assert(index() <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeConstIterator operator+(difference_type n) const { return PTreeNodeConstIterator(node(), index() + n); }
  PTreeNodeConstIterator operator-(difference_type n) const { return PTreeNodeConstIterator(node(), index() - n); }
  difference_type operator-(const PTreeNodeConstIterator& other) const {
    return static_cast<difference_type>(index()) - static_cast<difference_type>(other.index());
  }

  auto operator<=>(const PTreeNodeConstIterator& other) const {
    const auto res = node() <=> other.node();
    if (res != 0)
      return res;
    return index() <=> other.index();
  }

  bool operator==(const PTreeNodeConstIterator& other) const {
    return node() == other.node() && index() == other.index();
  }

  reference operator[](difference_type n) const { return *(*this + n); }

  bool is_begin() const { return index() == 0; }
  bool is_end() const {
    return index() == node_values_capacity<T>::value ||
           (index() > 0 && node_get_full_key(*value_.key.node.get(), value_.key.index) == 0);
  }

 protected:
  node_ref<T> node() const { return value_.key.node; }
  size_t index() const { return value_.key.index; }

  PTreeNodeIteratorValueRef<T> value_;
};

template <is_node_details T>
class PTreeNodeIterator : public PTreeNodeConstIterator<T> {
 public:
  using base = PTreeNodeConstIterator<T>;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = PTreeNodeIteratorValue<T>;

  using ref_type = PTreeNodeIteratorValueRef<T>;

  using reference = ref_type;
  using pointer = ref_type*;

  PTreeNodeIterator() = default;
  PTreeNodeIterator(node_ref<T> node, size_t index) : base(std::move(node), index) {}

  reference operator*() const { return base::value_; }

  pointer operator->() const { return const_cast<pointer>(base::operator->()); }

  PTreeNodeIterator& operator++() {
    base::operator++();
    return *this;
  }

  PTreeNodeIterator& operator--() {
    base::operator--();
    return *this;
  }

  PTreeNodeIterator operator++(int) { return PTreeNodeIterator(base::node(), base::index() + 1); }

  PTreeNodeIterator operator--(int) { return PTreeNodeIterator(base::node(), base::index() - 1); }

  PTreeNodeIterator& operator+=(difference_type n) {
    base::operator+=(n);
    return *this;
  }
  PTreeNodeIterator& operator-=(difference_type n) {
    base::operator-=(n);
    return *this;
  }
  PTreeNodeIterator operator+(difference_type n) const { return PTreeNodeIterator<T>(base::node(), base::index() + n); }
  PTreeNodeIterator operator-(difference_type n) const { return PTreeNodeIterator<T>(base::node(), base::index() - n); }
  difference_type operator-(const PTreeNodeIterator& other) const { return base::operator-(other); }
  difference_type operator-(const PTreeNodeConstIterator<T>& other) const { return base::operator-(other); }

  reference operator[](difference_type n) const { return *(*this + n); }
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

  PTreeNode(node_ref<T> node) : PTreeNode(node, node_values_size(*node.get())) {}
  PTreeNode(node_ref<T> node, size_t size) : node_(std::move(node)), size_(size) {}

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

  const T* node() { return node_.get(); }

 private:
  node_ref<T> node_;
  size_t size_;
};
