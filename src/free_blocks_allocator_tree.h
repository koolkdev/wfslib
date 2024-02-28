/*
 * Copyright (C) 2022 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <memory>
#include <optional>

#include "structs.h"
#include "tree_nodes_allocator.h"

template <typename T>
concept has_keys = std::is_array<decltype(T::keys)>::value &&
                   std::is_convertible<std::remove_extent_t<decltype(T::keys)>, uint32_be_t>::value;

template <typename T>
concept has_array_values = std::is_array<decltype(T::values)>::value;

template <typename T>
concept has_nibble_values = std::is_convertible<decltype(T::values), uint32_be_t>::value;

template <typename T>
concept has_values = has_array_values<T> || has_nibble_values<T>;

template <typename T>
concept is_node_details = has_keys<T> && has_values<T>;

enum class nibble : uint8_t { _0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _a, _b, _c, _d, _e, _f };

template <typename T, typename U>
concept is_array_value_type =
    has_array_values<T> && std::is_convertible<std::remove_extent_t<decltype(T::values)>, U>::value;

template <bool Condition, typename T>
using conditional_type_pair = std::pair<std::bool_constant<Condition>, T>;
template <typename... Pairs>
struct conditions_are;
template <>
struct conditions_are<> {
  using type = void;
};
template <typename T, typename... Pairs>
struct conditions_are<conditional_type_pair<true, T>, Pairs...> {
  using type = T;
};
template <typename T, typename... Pairs>
struct conditions_are<conditional_type_pair<false, T>, Pairs...> {
  using type = typename conditions_are<Pairs...>::type;
};
template <typename... Pairs>
using conditional_types = typename conditions_are<Pairs...>::type;

template <has_values T>
struct node_value_type {
  using type = conditional_types<conditional_type_pair<has_nibble_values<T>, nibble>,
                                 conditional_type_pair<is_array_value_type<T, uint32_be_t>, uint32_t>,
                                 conditional_type_pair<is_array_value_type<T, uint16_be_t>, uint16_t>>;
};

template <has_keys T>
struct node_keys_capacity {
  constexpr static size_t value = std::extent<decltype(T::keys)>::value;
};

using key_type = uint32_t;

template <has_keys T>
struct node_access_key {
  static key_type get(const T& node, size_t i) { return node.keys[i].value(); }
  static void set(T& node, size_t i, key_type value) { node.keys[i] = value; }
};
template <size_t index, has_keys T>
  requires(index < node_keys_capacity<T>::value)
key_type node_get_key(const T& node) {
  return node_access_key<T>::get(node, index);
}
template <has_keys T>
key_type node_get_key(const T& node, size_t index) {
  assert(index < node_keys_capacity<T>::value);
  return node_access_key<T>::get(node, index);
}
template <size_t index, has_keys T>
  requires(index < node_keys_capacity<T>::value)
void node_set_key(T& node, key_type value) {
  node_access_key<T>::set(node, index, value);
}
template <has_keys T>
void node_set_key(T& node, size_t index, key_type value) {
  assert(index < node_keys_capacity<T>::value);
  node_access_key<T>::set(node, index, value);
}

template <typename T>
struct node_values_capacity;
template <has_array_values T>
struct node_values_capacity<T> {
  constexpr static size_t value = std::extent<decltype(T::values)>::value;
};
template <has_nibble_values T>
struct node_values_capacity<T> {
  constexpr static size_t value = 7;  // specific for the nibbles node...
};

template <typename T>
struct node_access_value;
template <has_array_values T>
struct node_access_value<T> {
  static node_value_type<T>::type get(const T& node, size_t i) { return node.values[i].value(); }
  static void set(T& node, size_t i, node_value_type<T>::type value) { node.values[i] = value; }
};
template <has_nibble_values T>
struct node_access_value<T> {
  static node_value_type<T>::type get(const T& node, size_t i) {
    return static_cast<nibble>((node.values.value() >> (4 * i)) & 0xf);
  }
  static void set(T& node, size_t i, node_value_type<T>::type value) {
    node.values = (node.values.value() & ~(0xf << (4 * i))) | (value << (4 * i));
  }
};
template <size_t index, has_values T>
  requires(index < node_values_capacity<T>::value)
node_value_type<T>::type node_get_value(const T& node) {
  return node_access_value<T>::get(node, index);
}
template <has_values T>
node_value_type<T>::type node_get_value(const T& node, size_t index) {
  assert(index < node_values_capacity<T>::value);
  return node_access_value<T>::get(node, index);
}
template <size_t index, has_values T>
  requires(index < node_values_capacity<T>::value)
void node_set_value(T& node, typename node_value_type<T>::type value) {
  node_access_value<T>::set(node, index, value);
}
template <has_values T>
void node_set_value(T& node, size_t index, typename node_value_type<T>::type value) {
  assert(index < node_values_capacity<T>::value);
  node_access_value<T>::set(node, index, value);
}

template <has_keys T, int low, int high>
struct node_keys_size_calc;
template <has_keys T, int low, int high>
  requires(low > high)
struct node_keys_size_calc<T, low, high> {
  static size_t value([[maybe_unused]] const T& node) { return size_t{low}; }
};
template <has_keys T, int low, int high>
  requires(0 <= low && low <= high && high < node_keys_capacity<T>::value)
struct node_keys_size_calc<T, low, high> {
  static size_t value(const T& node) {
    constexpr int mid = low + (high - low) / 2;
    return node_get_key<mid>(node) == 0 ? node_keys_size_calc<T, low, mid - 1>::value(node)
                                        : node_keys_size_calc<T, mid + 1, high>::value(node);
  }
};

template <typename T>
concept is_parent_node_details = is_node_details<T> && node_keys_capacity<T>::value + 1 ==
node_values_capacity<T>::value;
template <typename T>
concept is_leaf_node_details = is_node_details<T> && node_keys_capacity<T>::value ==
node_values_capacity<T>::value;

template <has_keys T>
size_t node_keys_size(const T& node);
template <is_parent_node_details T>
size_t node_keys_size(const T& node) {
  return node_keys_size_calc<T, 0, node_keys_capacity<T>::value - 1>::value(node);
}
template <is_leaf_node_details T>
size_t node_keys_size(const T& node) {
  return std::max(node_keys_size_calc<T, 0, node_keys_capacity<T>::value - 1>::value(node), size_t{1});
}

template <is_node_details T>
size_t node_values_size(const T& node);
template <is_parent_node_details T>
size_t node_values_size(const T& node) {
  return node_keys_size(node) + 1;
}
template <is_leaf_node_details T>
size_t node_values_size(const T& node) {
  return node_keys_size(node);
}

template <has_keys T>
key_type node_get_full_key(const T& node, size_t index);
template <has_keys T>
  requires is_parent_node_details<T>
key_type node_get_full_key(const T& node, size_t index) {
  return index == 0 ? 0 : node_get_key(node, index - 1);
}
template <has_keys T>
  requires is_leaf_node_details<T>
key_type node_get_full_key(const T& node, size_t index) {
  return node_get_key(node, index);
}

template <has_keys T>
void node_set_full_key(T& node, size_t index, key_type key);
template <has_keys T>
  requires is_parent_node_details<T>
void node_set_full_key(T& node, size_t index, key_type key) {
  if (index != 0)
    node_set_key(node, index - 1, key);
}
template <has_keys T>
  requires is_leaf_node_details<T>
void node_set_full_key(T& node, size_t index, key_type key) {
  return node_set_key(node, index, key);
}

static_assert(std::is_same_v<uint16_t, node_value_type<RTreeNode_details>::type>);
static_assert(node_keys_capacity<RTreeNode_details>::value == 5);
static_assert(node_values_capacity<RTreeNode_details>::value == 6);
static_assert(std::is_same_v<uint32_t, node_value_type<RTreeLeaf_details>::type>);
static_assert(node_keys_capacity<RTreeLeaf_details>::value == 4);
static_assert(node_values_capacity<RTreeLeaf_details>::value == 4);
static_assert(std::is_same_v<nibble, node_value_type<FTreeLeaf_details>::type>);
static_assert(node_keys_capacity<FTreeLeaf_details>::value == 7);
static_assert(node_values_capacity<FTreeLeaf_details>::value == 7);

template <typename T, typename U>
concept nodes_allocator_methods = requires(T& allocator, U* node_type) {
                                    { allocator.template get_mutable_object<U>(uint16_t{0}) } -> std::same_as<U*>;
                                    { allocator.template Alloc<U>(uint16_t{0}) } -> std::same_as<U*>;
                                    allocator.template Free<U>(node_type, uint16_t{0});
                                  } && requires(const T& allocator, const U* node_type) {
                                         { allocator.template get_object<U>(uint16_t{0}) } -> std::same_as<const U*>;
                                         { allocator.template to_offset<U>(node_type) } -> std::same_as<uint16_t>;
                                       };
template <typename T>
concept nodes_allocator_construct = std::constructible_from<T, std::shared_ptr<MetadataBlock>>;

template <typename T, typename U>
concept nodes_allocator = nodes_allocator_methods<T, U> && nodes_allocator_construct<T>;

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
struct node_ref {
  const Allocator* allocator;
  uint16_t offset;

  const T* get() const { return allocator->template get_object<T>(offset); }
  T* get_mutable() const { return allocator->template get_mutable_object<T>(offset); }
  auto operator<=>(const node_ref& other) const { return get() <=> other.get(); }
  bool operator==(const node_ref& other) const { return get() == other.get(); }
};

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
class PTreeNodeKeyRef {
 public:
  PTreeNodeKeyRef(node_ref<T, Allocator> node, size_t index) : node(std::move(node)), index(index) {}

  operator key_type() const {
    assert(index < node_values_size(*node.get()));
    return node_get_full_key(*node.get(), index);
  }

  PTreeNodeKeyRef& operator=(key_type val) {
    node_set_full_key(*node.get_mutable(), index, val);
    return *this;
  }

 private:
  node_ref<T, Allocator> node;
  size_t index;
};

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
class PTreeNodeValueRef {
 public:
  PTreeNodeValueRef(node_ref<T, Allocator> node, size_t index) : node(std::move(node)), index(index) {}

  operator node_value_type<T>::type() const {
    assert(index < node_values_size(*node.get()));
    return node_get_value(*node.get(), index);
  }

  PTreeNodeValueRef& operator=(node_value_type<T>::type val) {
    node_set_value(*node.get_mutable(), index, val);
    return *this;
  }

 private:
  node_ref<T, Allocator> node;
  size_t index;
};

template <is_node_details T>
struct PTreeNodeIteratorValue {
  const key_type key;
  const typename node_value_type<T>::type value;

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

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
struct PTreeNodeIteratorValueRef {
  PTreeNodeKeyRef<T, Allocator> key;
  PTreeNodeValueRef<T, Allocator> value;

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

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
class PTreeNodeConstIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = PTreeNodeIteratorValue<T>;

  using ref_type = PTreeNodeIteratorValueRef<T, Allocator>;

  using const_reference = value_type;
  using reference = const_reference;
  using pointer = const_reference*;

  PTreeNodeConstIterator() = default;
  PTreeNodeConstIterator(node_ref<T, Allocator> node, size_t index) : node(std::move(node)), index(index) {
    assert(index <= node_keys_capacity<T>::value);
  }

  reference operator*() const {
    auto* _node = node.get();
    assert(index < node_values_size(*_node));
    return {node_get_full_key(*_node, index), node_get_value(*_node, index)};
  }

  PTreeNodeConstIterator& operator++() {
    ++index;
    return *this;
  }

  PTreeNodeConstIterator& operator--() {
    --index;
    return *this;
  }

  PTreeNodeConstIterator operator++(int) { return PTreeNodeConstIterator(node, index + 1); }

  PTreeNodeConstIterator operator--(int) { return PTreeNodeConstIterator(node, index - 1); }

  PTreeNodeConstIterator& operator+=(difference_type n) {
    index += n;
    assert(index <= node_keys_capacity<T>::value);
    return *this;
  }
  PTreeNodeConstIterator& operator-=(difference_type n) {
    index -= n;
    assert(index <= node_keys_capacity<T>::value);
    return *this;
  }
  PTreeNodeConstIterator operator+(difference_type n) const { return PTreeNodeConstIterator(node, index + n); }
  PTreeNodeConstIterator operator-(difference_type n) const { return PTreeNodeConstIterator(node, index - n); }
  difference_type operator-(const PTreeNodeConstIterator& other) const {
    return static_cast<int>(index) - static_cast<int>(other.index);
  }

  auto operator<=>(const PTreeNodeConstIterator& other) const {
    const auto res = node <=> other.node;
    if (res != 0)
      return res;
    return index <=> other.index;
  }

  bool operator==(const PTreeNodeConstIterator& other) const { return node == other.node && index == other.index; }

  reference operator[](difference_type n) const { return *(*this + n); }

 protected:
  node_ref<T, Allocator> node;
  size_t index;
};

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
class PTreeNodeIterator : public PTreeNodeConstIterator<T, Allocator> {
 public:
  using base = PTreeNodeConstIterator<T, Allocator>;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = PTreeNodeIteratorValue<T>;

  using ref_type = PTreeNodeIteratorValueRef<T, Allocator>;

  using const_reference = value_type;
  using reference = ref_type;
  using pointer = ref_type*;

  PTreeNodeIterator() = default;
  PTreeNodeIterator(node_ref<T, Allocator> node, size_t index) : base(node, index) {}

  reference operator*() const {
    assert(base::index < node_keys_capacity<T>::value);
    return {{base::node, base::index}, {base::node, base::index}};
  }

  PTreeNodeIterator& operator++() {
    ++base::index;
    return *this;
  }

  PTreeNodeIterator& operator--() {
    --base::index;
    return *this;
  }

  PTreeNodeIterator operator++(int) { return PTreeNodeIterator(base::node, base::index + 1); }

  PTreeNodeIterator operator--(int) { return PTreeNodeIterator(base::node, base::index - 1); }

  PTreeNodeIterator& operator+=(difference_type n) {
    base::index += n;
    assert(base::index <= node_keys_capacity<T>::value);
    return *this;
  }
  PTreeNodeIterator& operator-=(difference_type n) {
    base::index -= n;
    assert(base::index <= node_keys_capacity<T>::value);
    return *this;
  }
  PTreeNodeIterator operator+(difference_type n) const {
    return PTreeNodeIterator<T, Allocator>(base::node, base::index + n);
  }
  PTreeNodeIterator operator-(difference_type n) const {
    return PTreeNodeIterator<T, Allocator>(base::node, base::index - n);
  }
  difference_type operator-(const PTreeNodeIterator& other) const {
    return static_cast<int>(base::index) - static_cast<int>(other.index);
  }

  reference operator[](difference_type n) const { return *(*this + n); }
};

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
PTreeNodeConstIterator<T, Allocator> operator+(typename PTreeNodeConstIterator<T, Allocator>::difference_type n,
                                               const PTreeNodeConstIterator<T, Allocator>& iter) {
  return iter + n;
}

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T>
PTreeNodeIterator<T, Allocator> operator+(typename PTreeNodeIterator<T, Allocator>::difference_type n,
                                          const PTreeNodeIterator<T, Allocator>& iter) {
  return iter + n;
}

template <is_node_details T, typename Allocator>
  requires nodes_allocator<Allocator, T> && std::random_access_iterator<PTreeNodeIterator<T, Allocator>> &&
           std::random_access_iterator<PTreeNodeConstIterator<T, Allocator>>
class PTreeNode {
 public:
  using iterator = PTreeNodeIterator<T, Allocator>;
  using const_iterator = PTreeNodeConstIterator<T, Allocator>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  PTreeNode(node_ref<T, Allocator> node) : PTreeNode(std::move(node), node_values_size(*node_.get())) {}
  PTreeNode(node_ref<T, Allocator> node, size_t size) : node_(std::move(node)), size_(size) {}

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
  const_reverse_iterator crbegin() const { return rend(); }
  const_reverse_iterator crend() const { return rbegin(); }

  bool full() { return size_ == node_keys_capacity<T>::value; }
  bool empty() { return size_ == 0; }

  iterator find(key_type key, bool exact_match) {
    auto it = std::upper_bound(begin(), end(), key);
    if (it != begin())
      --it;
    if (exact_match && (it == end() || (*it).key != key))
      return end();
    return it;
  }

  void insert(iterator pos, typename iterator::value_type key_val) {
    assert(size_ < node_keys_capacity<T>::value);
    assert(cbegin() <= pos <= cend());
    std::copy<const_iterator, iterator>(pos, cend(), pos + 1);
    *pos = key_val;
    size_++;
  }

  void insert(iterator pos, const_iterator it_start, const_iterator it_end) {
    assert(size_ + (it_end - it_start) <= node_keys_capacity<T>::value);
    assert(cbegin() <= pos <= cend());
    std::copy<const_iterator, iterator>(pos, cend(), pos + (it_end - it_start));
    std::copy<const_iterator, iterator>(it_start, it_end, pos);
    size_ += it_end - it_start;
  }

  void erase(iterator pos) {
    assert(cbegin() <= pos <= cend());
    auto new_end = end() - 1;
    std::copy<const_iterator, iterator>(pos + 1, cend(), pos);
    std::fill(new_end, end(), 0);
    --size_;
  }

  void erase(iterator it_start, iterator it_end) {
    assert(cbegin() <= it_start <= cend());
    assert(cbegin() <= it_end <= cend());
    assert(it_start <= it_end);
    auto new_end = it_start + (cend() - it_end);
    std::copy<const_iterator, iterator>(it_end, cend(), it_start);
    std::fill(new_end, end(), 0);
    size_ -= it_end - it_start;
  }

  void clear(bool full = false) {
    std::fill(begin(), full ? (begin() + node_keys_capacity<T>::value) : end(), 0);
    size_ = 0;
  }

  // TODO: Out from this class
  iterator split_point(iterator pos, key_type& split_key)
    requires std::same_as<T, FTreeLeaf_details>
  {
    assert(cbegin() <= pos <= cend());
    assert(full());
    auto res = pos;
    switch (pos - begin()) {
      case 0:
      case 1:
      case 2:
      case 3:
        res = begin() + 3;
        break;
      case 4:
        return pos;
      case 5:
      case 6:
      case 7:
        res = begin() + 4;
        break;
    }
    split_key = (*res).key;
    return res;
  }

  iterator split_point(iterator pos, key_type& split_key)
    requires std::same_as<T, RTreeLeaf_details>
  {
    assert(cbegin() <= pos <= cend());
    assert(full());
    auto res = pos;
    switch (pos - begin()) {
      case 0:
      case 1:
        res = begin() + 1;
        break;
      case 2:
        res = begin() + 2;
        break;
      case 3:
        return pos;
      case 4:
        res = begin() + 3;
        break;
    }
    split_key = (*res).key;
    return res;
  }

  iterator split_point(iterator pos, key_type& split_key)
    requires std::same_as<T, RTreeNode_details>
  {
    assert(cbegin() <= pos <= cend());
    assert(full());
    auto res = pos;
    switch (pos - begin()) {
      case 0:
      case 1:
      case 2:
        res = begin() + 3;
        break;
      case 3:
        return pos + 1;
      case 4:
      case 5:
        res = begin() + 4;
        break;
    }
    split_key = (*res).key;
    return res;
  }

  bool operator==(const PTreeNode& other) const { return node_ == other.node_; }

 private:
  node_ref<T, Allocator> node_;
  size_t size_;
};

template <typename T>
struct node_iterator_info {
  T node;
  typename T::iterator iterator;
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename Allocator>
  requires nodes_allocator<Allocator, ParentNodeDetails> && nodes_allocator<Allocator, LeafNodeDetails>
class PTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = typename PTreeNodeIterator<LeafNodeDetails, Allocator>::value_type;
  using iterator_type = PTreeIterator<ParentNodeDetails, LeafNodeDetails, Allocator>;

  using parent_node_info = node_iterator_info<PTreeNode<ParentNodeDetails, Allocator>>;
  using leaf_node_info = node_iterator_info<PTreeNode<LeafNodeDetails, Allocator>>;

  PTreeIterator() = default;
  PTreeIterator(const Allocator* allocator, std::vector<parent_node_info> parents, std::optional<leaf_node_info> leaf)
      : allocator_(allocator), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

  value_type operator*() { return *leaf_->iterator; }

  iterator_type& operator++() {
    if (++leaf_->iterator == leaf_->node.end()) {
      auto parent = parents_.end();
      if (parent == parents_.begin())
        return *this;  // end
      for (--parent; ++parent->iterator == parent->node.end(); --parent) {
        ++parent->iterator;
        if (parent == parents_.begin())
          return *this;  // end
      }
      uint16_t node_offset = (*parent->iterator).value;  // TODO: by ref
      for (++parent; parent != parents_.end(); ++parent) {
        parent->node = {{allocator_, node_offset}};
        parent->iterator = parent->node.begin();
        node_offset = (*parent->iterator).value;  // TODO: by ref
      }
      leaf_->node = {{allocator_, node_offset}};
      leaf_->iterator = leaf_->node.begin();
    }
    return *this;
  }

  iterator_type& operator--() {
    if (leaf_->iterator == leaf_->node.begin()) {
      auto parent = parents_.end();
      if (parent == parents_.begin())
        return *this;  // begin
      for (--parent; parent->iterator == parent->node.begin(); --parent) {
        if (parent == parents_.begin())
          return *this;  // begin
      }
      --parent->iterator;
      uint16_t node_offset = (*parent->iterator).value;  // TODO: by ref
      for (++parent; parent != parents_.end(); ++parent) {
        parent->node = {{allocator_, node_offset}};
        parent->iterator = parent->node.end();
        --parent->iterator;
        node_offset = (*parent->iterator).value;  // TODO: by ref
      }
      leaf_->node = {{allocator_, node_offset}};
      leaf_->iterator = leaf_->node.end();
    }
    --leaf_->iterator;
    return *this;
  }

  iterator_type operator++(int) {
    iterator_type tmp(*this);
    ++(*this);
    return tmp;
  }

  iterator_type operator--(int) {
    iterator_type tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const iterator_type& other) const {
    if (!leaf_ || !other.leaf_)
      return !other.leaf_ && !other.leaf_;
    return leaf_->node == other.leaf_->node && leaf_->iterator == other.leaf_->iterator;
  }
  bool operator!=(const iterator_type& other) const { return !operator==(other); }

  leaf_node_info& leaf() { return *leaf_; }
  std::vector<parent_node_info>& parents() { return parents_; };

 private:
  const Allocator* allocator_;
  std::vector<parent_node_info> parents_;
  std::optional<leaf_node_info> leaf_;
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename Allocator>
  requires nodes_allocator<Allocator, ParentNodeDetails> && nodes_allocator<Allocator, LeafNodeDetails>
class PTree : public Allocator {
 public:
  using iterator = PTreeIterator<ParentNodeDetails, LeafNodeDetails, Allocator>;
  using const_iterator = const iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  using parent_node = PTreeNode<ParentNodeDetails, Allocator>;
  using leaf_node = PTreeNode<LeafNodeDetails, Allocator>;

  PTree(std::shared_ptr<MetadataBlock> block) : Allocator(std::move(block)) {}

  virtual PTreeHeader* mutable_header() = 0;
  virtual const PTreeHeader* header() const = 0;

  size_t size() const { return header()->items_count.value(); }
  iterator begin() const {
    if (size() == 0)
      return iterator(this, {}, std::nullopt);
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      parent_node node{{this, node_offset}};
      parents.push_back({node, node.begin()});
      node_offset = (*parents.back().iterator).value;
    }
    leaf_node leaf{{this, node_offset}};
    return iterator(this, std::move(parents), {{leaf, leaf.begin()}});
  }
  iterator end() const {
    if (size() == 0)
      return iterator(this, {}, std::nullopt);
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      parent_node node{{this, node_offset}};
      parents.push_back({node, node.end()});
      --parents.back().iterator;
      node_offset = (*parents.back().iterator).value;
    }
    leaf_node leaf{{this, node_offset}};
    return iterator(this, std::move(parents), {{leaf, leaf.end()}});
  }
  // TODO:
  // const_iterator begin() const { return cbegin(); }
  // const_iterator end() const { return cend(); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  iterator find(key_type key, bool exact_match = true) {
    if (size() == 0)
      return end();
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      parent_node node{{this, node_offset}};
      auto next_node = node.find(key, false);
      if (next_node == node.end())
        --next_node;
      parents.push_back({node, next_node});
      node_offset = (*parents.back().iterator).value;
    }
    leaf_node leaf{{this, node_offset}};
    return iterator(this, std::move(parents), {{leaf, leaf.find(key, exact_match)}});
  }

  LeafNodeDetails* grow_tree(iterator pos, key_type split_key) {
    uint16_t nodes_to_alloc = 2;
    auto parent = pos.parents().rbegin();
    while (parent != pos.parents().rend() && parent->node.full()) {
      ++nodes_to_alloc;
      ++parent;
    }
    if (parent == pos.parents().rend() && pos.parents().size() == 4) {
      // can't grow anymore in depth
      return nullptr;
    }
    auto* nodes = this->template Alloc<ParentNodeDetails>(nodes_to_alloc);
    if (!nodes)
      return nullptr;
    auto* node = &nodes[1];
    auto child_node_offset = this->to_offset(&nodes[0]);
    auto child_split_key = split_key;
    for (parent = pos.parents().rbegin(); parent != pos.parents().rend() && parent->node.full(); ++node, ++parent) {
      auto parent_split_pos = parent->node.split_point(parent->iterator, split_key);
      parent_node new_node{{this, this->to_offset(node)}, 0};
      new_node.clear(true);
      new_node.insert(new_node.begin(), parent_split_pos, parent->node.end());
      parent->node.erase(parent_split_pos, parent->node.end());
      if (child_split_key >= split_key) {
        new_node.insert(new_node.begin() + ((parent->iterator - parent_split_pos) + 1),
                        {child_split_key, child_node_offset});
      } else {
        parent->node.insert(parent->iterator + 1, {child_split_key, child_node_offset});
      }
      child_node_offset = this->to_offset(node);
      child_split_key = split_key;
    }
    if (parent == pos.parents().rend()) {
      // new root
      uint16_t node_offset = this->to_offset(node);
      parent_node new_node{{this, node_offset}, 0};
      new_node.clear(true);
      new_node.insert(new_node.end(), {0, header()->root_offset.value()});
      new_node.insert(new_node.end(), {child_split_key, child_node_offset});
      mutable_header()->root_offset = node_offset;
      mutable_header()->tree_depth = header()->tree_depth.value() + 1;
    } else {
      parent->node.insert(parent->iterator + 1, {child_split_key, child_node_offset});
    }
    return reinterpret_cast<LeafNodeDetails*>(&nodes[0]);
  }

  bool insert(typename iterator::value_type key_val) {
    auto items_count = header()->items_count.value();
    if (items_count == 0) {
      // first item in tree
      auto* node = this->template Alloc<LeafNodeDetails>(1);
      if (!node) {
        // Tree is full
        return false;
      }
      uint16_t node_offset = this->to_offset(node);
      leaf_node new_node{{this, node_offset}, 0};
      new_node.clear(true);
      new_node.insert(new_node.begin(), std::move(key_val));
      auto* header = mutable_header();
      header->items_count = 1;
      header->root_offset = node_offset;
      header->tree_depth = 0;
      return true;
    }
    auto pos = find(key_val.key, false);
    if (pos.leaf().iterator != pos.leaf().node.end() && (*pos).key == key_val.key) {
      // key already exists
      return false;
    }
    auto leaf_pos_to_insert = key_val.key < (*pos).key ? pos : pos + 1;
    if (!pos.leaf().node.full()) {
      // We have place to add the new key/val
      pos.leaf().node.insert(leaf_pos_to_insert, key_val);
      mutable_header()->items_count = items_count + 1;
      return true;
    }
    auto split_key = key_val.key;
    auto split_pos = pos.leaf().node.split_point(leaf_pos_to_insert, split_key);
    auto* node = grow_tree(pos, split_key);
    if (!node)
      return false;
    leaf_node new_node{{this, this->to_offset(node)}, 0};
    new_node.clear(true);
    new_node.insert(new_node.begin(), split_pos, pos.leaf().node.end());
    pos.leaf().node.erase(split_pos, pos.leaf().node.end());
    if (key_val.key >= split_key) {
      new_node.insert(new_node.begin() + (leaf_pos_to_insert - split_pos), key_val);
    } else {
      pos.leaf().node.insert(leaf_pos_to_insert, key_val);
    }
    mutable_header()->items_count = items_count + 1;
    return true;
  }

  void erase(iterator pos) {
    if (pos == end())
      return;
    pos.leaf().node.erase(pos.leaf().iterator);
    if (pos.leaf().node.empty()) {
      auto parent = pos.parents().rbegin();
      for (; parent != pos.parents().rend(); parent++) {
        parent.node.erase(parent.iterator);
        if (!parent.node.empty())
          break;
      }
      if (parent == pos.parents.rend()) {
        mutable_header()->tree_depth = 0;
      }
    }
    mutable_header()->items_count = header()->items_count.value() - 1;
  }

  void erase(key_type key) { erase(find(key)); }
};

static_assert(sizeof(RTreeNode_details) == sizeof(RTreeLeaf_details));
using EPTreeBlock = TreeNodesAllocator<FreeBlocksAllocatorHeader, EPTreeFooter, sizeof(RTreeNode_details)>;
static_assert(sizeof(RTreeNode_details) == sizeof(FTreeLeaf_details));
using FTreesBlock = TreeNodesAllocator<FTreesBlockHeader, FTreesFooter, sizeof(RTreeNode_details)>;

class RTree : public PTree<RTreeNode_details, RTreeLeaf_details, EPTreeBlock> {
 public:
  RTree(std::shared_ptr<MetadataBlock> block)
      : PTree<RTreeNode_details, RTreeLeaf_details, EPTreeBlock>(std::move(block)) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->current_tree; }
  const PTreeHeader* header() const override { return &tree_header()->current_tree; }
};

class FTree : public PTree<RTreeNode_details, FTreeLeaf_details, FTreesBlock> {
 public:
  FTree(std::shared_ptr<MetadataBlock> block, int block_size)
      : PTree<RTreeNode_details, FTreeLeaf_details, FTreesBlock>(std::move(block)), block_size_(block_size) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->trees[block_size_]; }
  const PTreeHeader* header() const override { return &tree_header()->trees[block_size_]; }

 private:
  int block_size_;
};

class EPTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::pair<uint32_t, std::array<FTree, 7>>;

  using node_info = node_iterator_info<RTree>;

  EPTreeIterator(std::vector<node_info> nodes, std::shared_ptr<Area> area)
      : nodes_(std::move(nodes)), area_(std::move(area)) {}

  template <std::size_t N, std::size_t... Is>
  std::array<FTree, N> CreateFTreeArray(uint32_t block_number, std::index_sequence<Is...>) {
    auto block = throw_if_error(area_->GetMetadataBlock(block_number));
    return {{FTree(block, Is)...}};
  }

  value_type operator*() {
    const auto& [key, block_number] = *nodes_.back().iterator;
    return {key, CreateFTreeArray<7>(block_number, std::make_index_sequence<7>())};
  }

  EPTreeIterator& operator++() {
    // TODO: Optimize?
    auto node = nodes_.end();
    for (--node; ++node->iterator == node->node.end(); --node) {
      --node->iterator;
      if (node == nodes_.begin())
        return *this;  // end
    }
    uint32_t node_block_number = (*node->iterator).value;  // todo: by ref
    for (++node; node != nodes_.end(); ++node) {
      node->node = {throw_if_error(area_->GetMetadataBlock(node_block_number))};
      node->iterator = node->node.begin();
      node_block_number = (*node->iterator).value;  // todo: by ref
    }
    return *this;
  }

  EPTreeIterator& operator--() {
    auto node = nodes_.end();
    for (--node; node->iterator == node->node.begin(); --node) {
      if (node == nodes_.begin())
        return *this;  // begin
    }
    --node->iterator;
    uint32_t node_block_number = (*node->iterator).value;  // todo: by ref
    for (++node; node != nodes_.end(); ++node) {
      node->node = {throw_if_error(area_->GetMetadataBlock(node_block_number))};
      node->iterator = node->node.end();
      --node->iterator;
      node_block_number = (*node->iterator).value;  // todo: by ref
    }
    return *this;
  }

  EPTreeIterator operator++(int) {
    EPTreeIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  EPTreeIterator operator--(int) {
    EPTreeIterator tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const EPTreeIterator& other) const {
    if (nodes_.empty() || other.nodes_.empty())
      return nodes_.empty() && other.nodes_.empty();
    return nodes_.back().iterator == other.nodes_.back().iterator;
  }
  bool operator!=(const EPTreeIterator& other) const { return !operator==(other); }

 private:
  std::vector<node_info> nodes_;
  std::shared_ptr<Area> area_;
};

class EPTree : public EPTreeBlock {
 public:
  using iterator = EPTreeIterator;
  using const_iterator = const iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  EPTree(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block)
      : EPTreeBlock(std::move(block)), area_(std::move(area)) {}

  // size_t size() { return header_->block_number; }
  iterator begin() const {
    if (tree_header()->current_tree.items_count.value() == 0)
      return iterator({}, area_);
    std::vector<typename iterator::node_info> nodes;
    uint32_t node_block_number = 0;
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      RTree node_tree(i == 0 ? original_block() : throw_if_error(area_->GetMetadataBlock(node_block_number)));
      nodes.push_back({node_tree, {}});
      nodes.back().iterator = nodes.back().node.begin();
      node_block_number = (*nodes.back().iterator).value;
    }
    return iterator(std::move(nodes), area_);
  }
  iterator end() const {
    if (tree_header()->current_tree.items_count.value() == 0)
      return iterator({}, area_);
    std::vector<typename iterator::node_info> nodes;
    uint32_t node_block_number = 0;
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      RTree node(i == 0 ? original_block() : throw_if_error(area_->GetMetadataBlock(node_block_number)));
      nodes.push_back({node, {}});
      nodes.back().iterator = nodes.back().node.end();
      --nodes.back().iterator;
      node_block_number = (*nodes.back().iterator).value;
    }
    return iterator(std::move(nodes), area_);
  }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

 private:
  std::shared_ptr<Area> area_;
};

class FreeBlocksAllocator : public EPTree {
 public:
  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block)
      : EPTree(std::move(area), std::move(block)) {}

  // void insert()

  // void free_blocks(uint32_t block_number, uint32_t count) {
  //  TODO: Ensure that not already free
  // }

  template <typename T>
  // requires T subclass of block
  std::vector<T> alloc_blocks(uint32_t count, Block::BlockSize size, uint32_t minimum_block_number = 0) {
    return {};
  }
};

// Iterator for specific size
class FreeBlocksAllocatorIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = FTree::iterator::difference_type;
  using value_type = FTree::iterator::value_type;

  using eptree_node_info = node_iterator_info<EPTree>;
  using ftree_node_info = node_iterator_info<FTree>;

  FreeBlocksAllocatorIterator(size_t block_size_index, eptree_node_info eptree, ftree_node_info ftree)
      : block_size_index(block_size_index), eptree(std::move(eptree)), ftree(std::move(ftree)) {}

  value_type operator*() { return *ftree.iterator; }

  FreeBlocksAllocatorIterator& operator++() {
    if (++ftree.iterator == ftree.node.end()) {
      if (++eptree.iterator == eptree.node.end()) {
        --eptree.iterator;
        return *this;  // end
      }
      ftree.node = (*eptree.iterator).second[block_size_index];
      ftree.iterator = ftree.node.begin();
    }
    return *this;
  }

  FreeBlocksAllocatorIterator& operator--() {
    if (ftree.iterator == ftree.node.begin()) {
      if (eptree.iterator == eptree.node.begin()) {
        return *this;  // begin
      }
      ftree.node = (*--eptree.iterator).second[block_size_index];
      ftree.iterator = ftree.node.end();
    }
    --ftree.iterator;
    return *this;
  }

 private:
  size_t block_size_index;

  eptree_node_info eptree;
  ftree_node_info ftree;
};
