/*
 * Copyright (C) 2022 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <variant>

#include "free_blocks_allocator.h"
#include "structs.h"
#include "tree_nodes_allocator.h"

// Log2 of number of block in for each single quanta in each bucket
constexpr size_t kSizeBuckets[] = {0, 3, 6, 10, 14, 18, 22};
constexpr size_t kSizeBucketsCount = std::extent<decltype(kSizeBuckets)>::value;

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
    node.values = (node.values.value() & ~(0xf << (4 * i))) | (static_cast<uint8_t>(value) << (4 * i));
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

template <typename T>
concept TreeIterator = requires(const T& iterator) {
                         { iterator.is_begin() } -> std::same_as<bool>;
                         { iterator.is_end() } -> std::same_as<bool>;
                       };

template <typename T>
  requires TreeIterator<T>
class TreeReverseIterator : public std::reverse_iterator<T> {
 public:
  TreeReverseIterator() = default;
  explicit TreeReverseIterator(T it) : std::reverse_iterator<T>(std::move(it)) {}
  TreeReverseIterator(const TreeReverseIterator& it) : std::reverse_iterator<T>(std::move(it)) {}

  TreeReverseIterator& operator++() {
    std::reverse_iterator<T>::operator++();
    return *this;
  }
  TreeReverseIterator& operator--() {
    std::reverse_iterator<T>::operator--();
    return *this;
  }
  TreeReverseIterator operator++(int) {
    TreeReverseIterator tmp(*this);
    ++(*this);
    return tmp;
  }
  TreeReverseIterator operator--(int) {
    TreeReverseIterator tmp(*this);
    --(*this);
    return tmp;
  }
  TreeReverseIterator& operator=(const TreeReverseIterator& other) {
    std::reverse_iterator<T>::operator=(other);
    return *this;
  }

  bool is_begin() const { return this->base().is_end(); }
  bool is_end() const { return this->base().is_begin(); }
};

template <typename T, typename U>
concept nodes_allocator_methods = requires(T& allocator, U* node_type) {
                                    { allocator.template get_mutable_object<U>(uint16_t{0}) } -> std::same_as<U*>;
                                    { allocator.template Alloc<U>(uint16_t{0}) } -> std::same_as<U*>;
                                    allocator.template Free<U>(node_type, uint16_t{0});
                                  } && requires(const T& allocator, const U* node_type) {
                                         { allocator.template get_object<U>(uint16_t{0}) } -> std::same_as<const U*>;
                                         { allocator.template to_offset<U>(node_type) } -> std::same_as<uint16_t>;
                                         { allocator.block() } -> std::same_as<std::shared_ptr<MetadataBlock>>;
                                       };
template <typename T>
concept nodes_allocator_construct = std::constructible_from<T, std::shared_ptr<MetadataBlock>>;

template <typename T, typename U>
concept nodes_allocator = nodes_allocator_methods<T, U> && nodes_allocator_construct<T>;

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
class PTreeNodeKeyRef {
 public:
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
  node_ref<T> node;
  size_t index;
};

template <is_node_details T>
class PTreeNodeValueRef {
 public:
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

struct FreeBlocksExtent {
  uint32_t key;
  nibble value;
  size_t bucket_index;

  uint32_t block_number() const { return key; }
  uint32_t blocks_count() const { return (static_cast<uint32_t>(value) + 1) << kSizeBuckets[bucket_index]; }
  uint32_t end_block_number() const { return block_number() + blocks_count(); }

  operator FreeBlocksExtentInfo() const { return {block_number(), blocks_count(), bucket_index}; }
};

struct FreeBlocksExtentRef {
  PTreeNodeKeyRef<FTreeLeaf_details> key;
  PTreeNodeValueRef<FTreeLeaf_details> value;
  const size_t bucket_index;

  operator FreeBlocksExtent() const { return {key, value, bucket_index}; }
  operator FreeBlocksExtentInfo() const { return {block_number(), blocks_count(), bucket_index}; }

  uint32_t block_number() const { return this->operator FreeBlocksExtent().block_number(); }
  uint32_t blocks_count() const { return this->operator FreeBlocksExtent().blocks_count(); }
  uint32_t end_block_number() const { return this->operator FreeBlocksExtent().end_block_number(); }
};

template <is_node_details T>
class PTreeNodeConstIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = PTreeNodeIteratorValue<T>;

  using ref_type = PTreeNodeIteratorValueRef<T>;

  using const_reference = value_type;
  using reference = const_reference;
  using pointer = const_reference*;

  PTreeNodeConstIterator() = default;
  PTreeNodeConstIterator(node_ref<T> node, size_t index) : node(std::move(node)), index(index) {
    assert(index <= node_values_capacity<T>::value);
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
    assert(index <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeConstIterator& operator-=(difference_type n) {
    index -= n;
    assert(index <= node_values_capacity<T>::value);
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

  bool is_begin() const { return index == 0; }
  bool is_end() const {
    return index == node_values_capacity<T>::value || (index > 0 && node_get_full_key(*node.get(), index) == 0);
  }

 protected:
  node_ref<T> node;
  size_t index;
};

template <is_node_details T>
class PTreeNodeIterator : public PTreeNodeConstIterator<T> {
 public:
  using base = PTreeNodeConstIterator<T>;

  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = PTreeNodeIteratorValue<T>;

  using ref_type = PTreeNodeIteratorValueRef<T>;

  using const_reference = value_type;
  using reference = ref_type;
  using pointer = ref_type*;

  PTreeNodeIterator() = default;
  PTreeNodeIterator(node_ref<T> node, size_t index) : base(node, index) {}

  reference operator*() const {
    assert(base::index < node_values_capacity<T>::value);
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
    assert(base::index <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeIterator& operator-=(difference_type n) {
    base::index -= n;
    assert(base::index <= node_values_capacity<T>::value);
    return *this;
  }
  PTreeNodeIterator operator+(difference_type n) const { return PTreeNodeIterator<T>(base::node, base::index + n); }
  PTreeNodeIterator operator-(difference_type n) const { return PTreeNodeIterator<T>(base::node, base::index - n); }
  difference_type operator-(const PTreeNodeIterator& other) const {
    return static_cast<int>(base::index) - static_cast<int>(other.index);
  }

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

  bool insert(iterator pos, const typename iterator::value_type& key_val) {
    if (size_ >= node_keys_capacity<T>::value)
      return false;
    assert(cbegin() <= pos && pos <= cend());
    std::copy<const_iterator, iterator>(pos, cend(), pos + 1);
    *pos = key_val;
    size_++;
    return true;
  }

  bool insert(iterator pos, const_iterator it_start, const_iterator it_end) {
    if (size_ + (it_end - it_start) > node_keys_capacity<T>::value)
      return false;
    assert(cbegin() <= pos && pos <= cend());
    std::copy<const_iterator, iterator>(pos, cend(), pos + (it_end - it_start));
    std::copy<const_iterator, iterator>(it_start, it_end, pos);
    size_ += it_end - it_start;
    return true;
  }

  void erase(iterator pos) {
    assert(cbegin() <= pos && pos < cend());
    auto new_end = end() - 1;
    std::copy<const_iterator, iterator>(pos + 1, cend(), pos);
    std::fill(new_end, end(), 0);
    --size_;
  }

  void erase(iterator it_start, iterator it_end) {
    assert(cbegin() <= it_start && it_start <= cend());
    assert(cbegin() <= it_end && it_end <= cend());
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
    assert(cbegin() <= pos && pos <= cend());
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
    assert(cbegin() <= pos && pos <= cend());
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
    assert(cbegin() <= pos && pos <= cend());
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
  node_ref<T> node_;
  size_t size_;
};

template <typename T, typename Iterator>
struct node_iterator_info_base {
  std::shared_ptr<T> node;
  Iterator iterator;

  node_iterator_info_base() = default;
  node_iterator_info_base(const node_iterator_info_base& other) : node(other.node), iterator(other.iterator) {}
  node_iterator_info_base(node_iterator_info_base&& other)
      : node(std::move(other.node)), iterator(std::move(other.iterator)) {}
  node_iterator_info_base(std::shared_ptr<T> n, Iterator it) : node(std::move(n)), iterator(std::move(it)) {}
  node_iterator_info_base(const T& n, Iterator it) : node(std::make_shared<T>(n)), iterator(std::move(it)) {}
  node_iterator_info_base(T&& n, Iterator it) : node(std::make_shared<T>(std::move(n))), iterator(std::move(it)) {}
  node_iterator_info_base(const T& n) : node(std::make_shared<T>(n)) {}
  node_iterator_info_base(T&& n) : node(std::make_shared<T>(std::move(n))) {}
  // template <typename... Args>
  // node_iterator_info_base(Args&&... args) : node(std::make_shared<T>(std::forward<Args>(args)...)) {}
  node_iterator_info_base& operator=(const node_iterator_info_base& other) {
    node = other.node;
    iterator = other.iterator;
    return *this;
  }
};

template <typename T>
using node_iterator_info = node_iterator_info_base<T, typename T::iterator>;
template <typename T>
using node_const_iterator_info = node_iterator_info_base<const T, typename T::const_iterator>;
template <typename T>
using node_reverse_iterator_info = node_iterator_info_base<T, typename T::reverse_iterator>;
template <typename T>
using node_const_reverse_iterator_info = node_iterator_info_base<const T, typename T::const_reverse_iterator>;

template <typename T, typename U>
concept is_const_iterator_info =
    std::same_as<node_const_iterator_info<T>, U> || std::same_as<node_const_reverse_iterator_info<T>, U>;
template <typename T, typename U>
concept is_reverse_iterator_info =
    std::same_as<node_reverse_iterator_info<T>, U> || std::same_as<node_const_reverse_iterator_info<T>, U>;

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails>
class PTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = typename PTreeNodeIterator<LeafNodeDetails>::value_type;
  using iterator_type = PTreeIterator<ParentNodeDetails, LeafNodeDetails>;

  using const_reference = typename PTreeNodeIterator<LeafNodeDetails>::const_reference;
  using reference = typename PTreeNodeIterator<LeafNodeDetails>::reference;
  using pointer = typename PTreeNodeIterator<LeafNodeDetails>::pointer;

  using parent_node_info = node_iterator_info<PTreeNode<ParentNodeDetails>>;
  using leaf_node_info = node_iterator_info<PTreeNode<LeafNodeDetails>>;

  PTreeIterator() = default;
  PTreeIterator(std::shared_ptr<Block> block, std::vector<parent_node_info> parents, std::optional<leaf_node_info> leaf)
      : block_(block), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

  reference operator*() const { return *leaf_->iterator; }

  iterator_type& operator++() {
    assert(!is_end());
    if ((++leaf_->iterator).is_end()) {
      if (parents_.empty())
        return *this;  // end
      auto rparent = parents_.rbegin();
      while ((++rparent->iterator).is_end()) {
        if (++rparent == parents_.rend()) {
          while (--rparent != parents_.rbegin())
            --rparent->iterator;
          return *this;  // end
        }
      }
      uint16_t node_offset = (*rparent->iterator).value;
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{{block_, node_offset}}};
        new_parent.iterator = new_parent.node->begin();
        *parent = std::move(new_parent);
        node_offset = (*parent->iterator).value;
      }
      leaf_node_info new_leaf{{{block_, node_offset}}};
      new_leaf.iterator = new_leaf.node->begin();
      leaf_ = std::move(new_leaf);
    }
    return *this;
  }

  iterator_type& operator--() {
    assert(!is_begin());
    if (leaf_->iterator.is_begin()) {
      if (parents_.empty())
        return *this;  // begin
      auto rparent = parents_.rbegin();
      while (rparent->iterator.is_begin()) {
        if (++rparent == parents_.rend())
          return *this;  // begin
      }
      uint16_t node_offset = (*--rparent->iterator).value;  // TODO: by ref
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{{block_, node_offset}}};
        new_parent.iterator = new_parent.node->end();
        --new_parent.iterator;
        *parent = std::move(new_parent);
        node_offset = (*parent->iterator).value;  // TODO: by ref
      }
      leaf_node_info new_leaf{{{block_, node_offset}}};
      new_leaf.iterator = new_leaf.node->end();
      leaf_ = std::move(new_leaf);
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
    return leaf_->iterator == other.leaf_->iterator;
  }

  leaf_node_info& leaf() { return *leaf_; }
  std::vector<parent_node_info>& parents() { return parents_; };

  bool is_begin() const {
    return !leaf_ ||
           (std::ranges::all_of(parents_, [](const parent_node_info& parent) { return parent.iterator.is_begin(); }) &&
            leaf_->iterator.is_begin());
  }
  bool is_end() const { return !leaf_ || leaf_->iterator.is_end(); }

 private:
  std::shared_ptr<Block> block_;
  std::vector<parent_node_info> parents_;
  std::optional<leaf_node_info> leaf_;
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename Allocator>
  requires nodes_allocator<Allocator, ParentNodeDetails> && nodes_allocator<Allocator, LeafNodeDetails> &&
           std::bidirectional_iterator<PTreeIterator<ParentNodeDetails, LeafNodeDetails>>
class PTree : public Allocator {
 public:
  using iterator = PTreeIterator<ParentNodeDetails, LeafNodeDetails>;
  using const_iterator = iterator;  // TODO:
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  using parent_node = PTreeNode<ParentNodeDetails>;
  using leaf_node = PTreeNode<LeafNodeDetails>;

  PTree(std::shared_ptr<MetadataBlock> block) : Allocator(std::move(block)) {}
  virtual ~PTree() = default;

  virtual PTreeHeader* mutable_header() = 0;
  virtual const PTreeHeader* header() const = 0;

  size_t size() const { return header()->items_count.value(); }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>
  Iterator tbegin() const {
    if (size() == 0)
      return {this->block(), {}, std::nullopt};
    std::vector<typename Iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename Iterator::parent_node_info parent{{{this->block(), node_offset}}};
      parent.iterator = parent.node->begin();
      parents.push_back(std::move(parent));
      node_offset = (*parents.back().iterator).value;
    }
    typename Iterator::leaf_node_info leaf{{{this->block(), node_offset}}};
    leaf.iterator = leaf.node->begin();
    return {this->block(), std::move(parents), std::move(leaf)};
  }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>
  Iterator tend() const {
    if (size() == 0)
      return {this->block(), {}, std::nullopt};
    std::vector<typename Iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename Iterator::parent_node_info parent{{{this->block(), node_offset}}};
      parent.iterator = parent.node->end();
      --parent.iterator;
      parents.push_back(std::move(parent));
      node_offset = (*parents.back().iterator).value;
    }
    typename Iterator::leaf_node_info leaf{{{this->block(), node_offset}}};
    leaf.iterator = leaf.node->end();
    return {this->block(), std::move(parents), std::move(leaf)};
  }

  template <typename ReverseIterator>
    requires std::is_same_v<ReverseIterator, reverse_iterator> ||
             std::is_same_v<ReverseIterator, const_reverse_iterator>
  ReverseIterator tbegin() const {
    if constexpr (std::is_same_v<ReverseIterator, reverse_iterator>) {
      return ReverseIterator{tend<iterator>()};
    } else {
      return ReverseIterator{tend<const_iterator>()};
    }
  }

  template <typename ReverseIterator>
    requires std::is_same_v<ReverseIterator, reverse_iterator> ||
             std::is_same_v<ReverseIterator, const_reverse_iterator>
  ReverseIterator tend() const {
    if constexpr (std::is_same_v<ReverseIterator, reverse_iterator>) {
      return ReverseIterator{tbegin<iterator>()};
    } else {
      return ReverseIterator{tbegin<const_iterator>()};
    }
  }

  auto begin() { return tbegin<iterator>(); }
  auto end() { return tend<iterator>(); }
  auto begin() const { return tbegin<const_iterator>(); }
  auto end() const { return tend<const_iterator>(); }

  auto rbegin() { return tbegin<reverse_iterator>(); }
  auto rend() { return tend<reverse_iterator>(); }
  auto rbegin() const { return tbegin<const_reverse_iterator>(); }
  auto rend() const { return tend<const_reverse_iterator>(); }

  const_iterator middle() const {
    auto it = begin();
    for ([[maybe_unused]] auto _ : std::views::iota(0, header()->items_count.value() / 2)) {
      ++it;
    }
    return it;
  }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  bool empty() const { return header()->items_count.value() == 0; }

  auto find(key_type key, bool exact_match = true) { return tfind<iterator>(key, exact_match); }
  auto find(key_type key, bool exact_match = true) const { return tfind<const_iterator>(key, exact_match); }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>
  Iterator tfind(key_type key, bool exact_match = true) const {
    if (size() == 0)
      return {this->block(), {}, std::nullopt};
    std::vector<typename Iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename Iterator::parent_node_info parent{{{this->block(), node_offset}}};
      parent.iterator = parent.node->find(key, false);
      assert(!parent.iterator.is_end());
      parents.push_back(std::move(parent));
      node_offset = (*parents.back().iterator).value;
    }
    typename Iterator::leaf_node_info leaf{{{this->block(), node_offset}}};
    leaf.iterator = leaf.node->find(key, exact_match);
    return {this->block(), std::move(parents), std::move(leaf)};
  }

  LeafNodeDetails* grow_tree(iterator pos, key_type split_key) {
    uint16_t nodes_to_alloc = 2;
    auto parent = pos.parents().rbegin();
    while (parent != pos.parents().rend() && parent->node->full()) {
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
    for (parent = pos.parents().rbegin(); parent != pos.parents().rend() && parent->node->full(); ++node, ++parent) {
      auto parent_split_pos = parent->node->split_point(parent->iterator, split_key);
      parent_node new_node{{this->block(), this->to_offset(node)}, 0};
      new_node.clear(true);
      new_node.insert(new_node.begin(), parent_split_pos, parent->node->end());
      parent->node->erase(parent_split_pos, parent->node->end());
      if (child_split_key >= split_key) {
        new_node.insert(new_node.begin() + ((parent->iterator - parent_split_pos) + 1),
                        {child_split_key, child_node_offset});
      } else {
        parent->node->insert(parent->iterator + 1, {child_split_key, child_node_offset});
      }
      child_node_offset = this->to_offset(node);
      child_split_key = split_key;
    }
    if (parent == pos.parents().rend()) {
      // new root
      uint16_t node_offset = this->to_offset(node);
      parent_node new_node{{this->block(), node_offset}, 0};
      new_node.clear(true);
      new_node.insert(new_node.end(), {0, header()->root_offset.value()});
      new_node.insert(new_node.end(), {child_split_key, child_node_offset});
      mutable_header()->root_offset = node_offset;
      mutable_header()->tree_depth = header()->tree_depth.value() + 1;
    } else {
      parent->node->insert(parent->iterator + 1, {child_split_key, child_node_offset});
    }
    return reinterpret_cast<LeafNodeDetails*>(&nodes[0]);
  }

  bool insert(const typename iterator::value_type& key_val) {
    auto items_count = header()->items_count.value();
    if (items_count == 0) {
      // first item in tree
      auto* node = this->template Alloc<LeafNodeDetails>(1);
      if (!node) {
        // Tree is full
        return false;
      }
      uint16_t node_offset = this->to_offset(node);
      leaf_node new_node{{this->block(), node_offset}, 0};
      new_node.clear(true);
      new_node.insert(new_node.begin(), key_val);
      auto* header = mutable_header();
      header->items_count = 1;
      header->root_offset = node_offset;
      header->tree_depth = 0;
      return true;
    }
    auto pos = find(key_val.key, false);
    if (!pos.leaf().iterator.is_end() && (*pos).key == key_val.key) {
      // key already exists
      return false;
    }
    auto leaf_pos_to_insert = key_val.key < (*pos).key ? pos.leaf().iterator : pos.leaf().iterator + 1;
    if (!pos.leaf().node->full()) {
      // We have place to add the new key/val
      pos.leaf().node->insert(leaf_pos_to_insert, key_val);
      mutable_header()->items_count = items_count + 1;
      return true;
    }
    auto split_key = key_val.key;
    auto split_pos = pos.leaf().node->split_point(leaf_pos_to_insert, split_key);
    auto* node = grow_tree(pos, split_key);
    if (!node)
      return false;
    leaf_node new_node{{this->block(), this->to_offset(node)}, 0};
    new_node.clear(true);
    new_node.insert(new_node.begin(), split_pos, pos.leaf().node->end());
    pos.leaf().node->erase(split_pos, pos.leaf().node->end());
    if (key_val.key >= split_key) {
      new_node.insert(new_node.begin() + (leaf_pos_to_insert - split_pos), key_val);
    } else {
      pos.leaf().node->insert(leaf_pos_to_insert, key_val);
    }
    mutable_header()->items_count = items_count + 1;
    return true;
  }

  bool insert(const iterator& it_start, const iterator& it_end) {
    for (const auto& val : std::ranges::subrange(it_start, it_end)) {
      if (!insert(val))
        return false;
    }
    return true;
  }

  bool insert_compact(const iterator& it_start, const iterator& it_end) {
    if (!empty()) {
      assert(false);
      return false;
    }

    // Fill up to 5 items in each edge
    auto* header = mutable_header();
    header->tree_depth = 0;
    std::vector<std::pair<key_type, uint16_t>> current_nodes;
    for (auto it = it_start; it != it_end;) {
      auto* node = this->template Alloc<LeafNodeDetails>(1);
      leaf_node new_node{{this->block(), this->to_offset(node)}, 0};
      new_node.clear(true);
      for (int i = 0; i < 5 && it != it_end; ++i, ++it) {
        new_node.insert(new_node.end(), *it);
        header->items_count += 1;
      }
      current_nodes.push_back({(*new_node.begin()).key, this->to_offset(node)});
    }

    // Create the tree
    while (current_nodes.size() > 1) {
      header->tree_depth += 1;
      std::vector<std::pair<key_type, uint16_t>> new_nodes;
      for (auto it = current_nodes.begin(); it != current_nodes.end(); ++it) {
        auto* node = this->template Alloc<ParentNodeDetails>(1);
        parent_node new_node{{this->block(), this->to_offset(node)}, 0};
        new_node.clear(true);
        for (int i = 0; i < 5 && it != current_nodes.end(); ++i, ++it) {
          new_node.insert(new_node.end(), {it->first, it->second});
        }
        new_nodes.push_back({(*new_node.begin()).key, this->to_offset(node)});
      }
      current_nodes.swap(new_nodes);
    }

    // Update the root
    header->root_offset = current_nodes[0].second;

    return true;
  }

  void erase(iterator& pos) {
    pos.leaf().node->erase(pos.leaf().iterator);
    if (pos.leaf().node->empty()) {
      auto parent = pos.parents().rbegin();
      for (; parent != pos.parents().rend(); parent++) {
        parent->node->erase(parent->iterator);
        if (!parent->node->empty())
          break;
      }
      if (parent == pos.parents().rend()) {
        mutable_header()->tree_depth = 0;
      }
    }
    mutable_header()->items_count = header()->items_count.value() - 1;
  }

  bool erase(key_type key) {
    auto it = find(key);
    if (it == end())
      return false;
    erase(it);
    return true;
    ;
  }

  void erase(iterator& it_start, iterator& it_end) {
    for (auto it = it_start; it != it_end; ++it) {
      erase(it);
    }
  }

  void split(PTree& left, PTree& right, const iterator& pos) const {
    left.insert(begin(), pos);
    right.insert(pos, end());
  }

  void split(PTree& right, iterator& pos) {
    auto end_pos = end();
    right.insert(pos, end_pos);
    erase(pos, end_pos);
  }

  void split_compact(PTree& left, PTree& right, const iterator& pos) const {
    left.insert_compact(begin(), pos);
    right.insert_compact(pos, end());
  }
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

  void Init(uint8_t depth) {
    EPTreeBlock::Init();
    mutable_tree_header()->depth = depth;
    mutable_tree_header()->block_number = block_->BlockNumber();
  }
};

class FTree : public PTree<RTreeNode_details, FTreeLeaf_details, FTreesBlock> {
 public:
  FTree(std::shared_ptr<MetadataBlock> block, size_t block_size_index)
      : PTree<RTreeNode_details, FTreeLeaf_details, FTreesBlock>(std::move(block)),
        block_size_index_(block_size_index) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->trees[block_size_index_]; }
  const PTreeHeader* header() const override { return &tree_header()->trees[block_size_index_]; }

  size_t index() const { return block_size_index_; }

 private:
  size_t block_size_index_;
};

template <typename ftree_info_type>
class FTreesIteratorBase {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;

  using value_type = FreeBlocksExtent;
  using ref_type = FreeBlocksExtentRef;

  using const_reference = value_type;

  using reference = std::conditional<is_const_iterator_info<FTree, ftree_info_type>, const_reference, ref_type>::type;

  using ftree_info = ftree_info_type;

  FTreesIteratorBase() = default;
  FTreesIteratorBase(std::array<ftree_info, kSizeBucketsCount> ftrees, size_t index)
      : ftrees_(std::move(ftrees)), index_(index) {}

  reference operator*() const {
    auto key_val = *ftrees_[index_].iterator;
    return {key_val.key, key_val.value, index_};
  }

  FTreesIteratorBase& operator++() {
    assert(!is_end());
    ++ftrees_[index_].iterator;
    index_ = find_next_extent_index(ftrees_);
    return *this;
  }

  bool operator==(const FTreesIteratorBase& other) const {
    return index_ == other.index_ && ftrees_[index_].iterator == other.ftrees_[other.index_].iterator;
  }

  bool is_begin() const {
    return std::ranges::all_of(ftrees_, [](const ftree_info& ftree) { return ftree.iterator.is_begin(); });
  }
  bool is_end() const { return ftrees_[index_].iterator.is_end(); }

  template <typename Range, typename Compare>
  static auto find_extent(Range& range, Compare comp) {
    if constexpr (is_reverse_iterator_info<FTree, ftree_info_type>) {
      return std::ranges::max_element(range, comp);
    } else {
      return std::ranges::min_element(range, comp);
    }
  }

  static size_t find_next_extent_index(const std::array<ftree_info, kSizeBucketsCount>& ftrees) {
    auto iterated_ftrees =
        ftrees | std::views::filter([](const ftree_info& ftree) { return !ftree.iterator.is_end(); });
    auto res = find_extent(iterated_ftrees, [](const ftree_info& a, const ftree_info& b) {
      return (*a.iterator).key < (*b.iterator).key;
    });
    return res != std::ranges::end(iterated_ftrees) ? res->node->index() : 0;
  }

 private:
  std::array<ftree_info, kSizeBucketsCount> ftrees_;
  size_t index_{0};
};

using FTreesForwardConstIterator = FTreesIteratorBase<node_const_iterator_info<FTree>>;
using FTreesBackwardConstIterator = FTreesIteratorBase<node_const_reverse_iterator_info<FTree>>;
using FTreesForwardIterator = FTreesIteratorBase<node_iterator_info<FTree>>;
using FTreesBackwardIterator = FTreesIteratorBase<node_reverse_iterator_info<FTree>>;

class FTrees {
 public:
  using iterator = FTreesForwardIterator;
  using const_iterator = FTreesForwardConstIterator;
  using reverse_iterator = FTreesBackwardIterator;
  using const_reverse_iterator = FTreesBackwardConstIterator;

  FTrees(std::shared_ptr<MetadataBlock> block)
      : ftrees_(CreateFTreeArray(std::move(block), std::make_index_sequence<kSizeBucketsCount>{})) {}

  size_t size() const {
    // TODO: llvm fold support
    // return *std::ranges::fold_right_last(
    // ftrees_ | std::views::transform([](const FTree& ftree) { return ftree.size(); }), std::plus<>());
    return std::accumulate(ftrees_.begin(), ftrees_.end(), size_t{0},
                           [](auto acc, const FTree& ftree) { return acc + ftree.size(); });
  }

  bool empty() const { return size() == 0; }

  template <typename Iterator>
  Iterator tbegin() const {
    std::array<typename Iterator::ftree_info, kSizeBucketsCount> ftrees_info;
    std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                   [](const FTree& ftree) -> typename Iterator::ftree_info {
                     return {ftree, ftree.tbegin<decltype(Iterator::ftree_info::iterator)>()};
                   });
    auto index = Iterator::find_next_extent_index(ftrees_info);
    return {std::move(ftrees_info), index};
  }

  template <typename Iterator>
  Iterator tend() const {
    std::array<typename Iterator::ftree_info, kSizeBucketsCount> ftrees_info;
    std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                   [](const FTree& ftree) -> typename Iterator::ftree_info {
                     return {ftree, ftree.tend<decltype(Iterator::ftree_info::iterator)>()};
                   });
    return {std::move(ftrees_info), 0};
  }

  template <typename Iterator>
  Iterator tfind(key_type key) const {
    std::array<typename Iterator::ftree_info, kSizeBucketsCount> ftrees_info;
    std::transform(ftrees_.begin(), ftrees_.end(), ftrees_info.begin(),
                   [key](const FTree& ftree) -> typename Iterator::ftree_info {
                     if constexpr (std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>) {
                       auto it = ftree.tfind<decltype(Iterator::ftree_info::iterator)>(key, false);
                       // We want the iterator to be AFTER the search point
                       if (!it.is_end() && (*it).key != key)
                         ++it;
                       return {ftree, std::move(it)};
                     } else if constexpr (std::is_same_v<Iterator, reverse_iterator>) {
                       return {ftree, FTree::reverse_iterator{ftree.tfind<FTree::iterator>(key, false)}};
                     } else if constexpr (std::is_same_v<Iterator, const_reverse_iterator>) {
                       return {ftree, FTree::const_reverse_iterator{ftree.tfind<FTree::const_iterator>(key, false)}};
                     }
                   });
    // Finding the minimum will give us the closest point above the key
    auto index = Iterator::find_next_extent_index(ftrees_info);
    return {std::move(ftrees_info), index};
  }

  auto begin() { return tbegin<iterator>(); }
  auto end() { return tend<iterator>(); }
  auto begin() const { return tbegin<const_iterator>(); }
  auto end() const { return tend<const_iterator>(); }

  auto cbegin() const { return begin(); }
  auto cend() const { return end(); }

  auto rbegin() { return tbegin<reverse_iterator>(); }
  auto rend() { return tend<reverse_iterator>(); }
  auto rbegin() const { return tbegin<const_reverse_iterator>(); }
  auto rend() const { return tend<const_reverse_iterator>(); }

  auto find(key_type key) { return tfind<iterator>(key); }
  auto rfind(key_type key) { return tfind<reverse_iterator>(key); }
  auto find(key_type key) const { return tfind<const_iterator>(key); }
  auto rfind(key_type key) const { return tfind<const_reverse_iterator>(key); }

  void split(FTrees& left, FTrees& right, key_type& split_point_key) {
    // Find the ftree with most items
    auto max_ftree_it = std::ranges::max_element(ftrees_, [](const FTree& a, const FTree& b) {
      return a.header()->items_count.value() < b.header()->items_count.value();
    });
    split_point_key = (*max_ftree_it->middle()).key;
    for (auto [old_ftree, left_ftree, right_ftree] : std::views::zip(ftrees_, left.ftrees_, right.ftrees_)) {
      old_ftree.split_compact(left_ftree, right_ftree, old_ftree.find(split_point_key, false));
    }
  }

  // TODO:
  // const_iterator begin() const { return const_iterator(node_, 0); }
  // const_iterator end() const { return const_iterator(node_, size_); }
  // const_iterator cbegin() const { return begin(); }
  // const_iterator cend() const { return end(); }

  // const_reverse_iterator rbegin() const { return {end()}; }
  // const_reverse_iterator rend() const { return {begin()}; }
  // const_reverse_iterator crbegin() const { return rend(); }
  // const_reverse_iterator crend() const { return rbegin(); }

  // const_iterator find(key_type key) const {}
  // const_reverse_iterator rfind(key_type key) const {}

  std::array<FTree, kSizeBucketsCount>& ftrees() { return ftrees_; }

 private:
  template <std::size_t... Is>
  static std::array<FTree, kSizeBucketsCount> CreateFTreeArray(std::shared_ptr<MetadataBlock> block,
                                                               std::index_sequence<Is...>) {
    return {{{block, Is}...}};
  }

  std::array<FTree, kSizeBucketsCount> ftrees_;
};

class EPTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = RTree::iterator::value_type;

  using const_reference = RTree::iterator::const_reference;
  using reference = RTree::iterator::reference;
  using pointer = RTree::iterator::pointer;

  using node_info = node_iterator_info<RTree>;

  EPTreeIterator() = default;

  EPTreeIterator(FreeBlocksAllocator* allocator, std::vector<node_info> nodes)
      : allocator_(allocator), nodes_(std::move(nodes)) {}

  reference operator*() const { return *nodes_.back().iterator; }

  EPTreeIterator& operator++() {
    assert(!is_end());
    auto rnode = nodes_.rbegin();
    while ((++rnode->iterator).is_end()) {
      if (++rnode == nodes_.rend()) {
        while (--rnode != nodes_.rbegin())
          --rnode->iterator;
        return *this;  // end
      }
    }
    uint32_t node_block_number = (*rnode->iterator).value;
    for (auto node = rnode.base(); node != nodes_.end(); ++node) {
      *node = {allocator_->LoadAllocatorBlock(node_block_number)};
      node->iterator = node->node->begin();
      node_block_number = (*node->iterator).value;
    }
    return *this;
  }

  EPTreeIterator& operator--() {
    assert(!is_begin());
    auto rnode = nodes_.rbegin();
    for (; rnode->iterator.is_begin(); rnode++) {
      if (rnode == nodes_.rend())
        return *this;  // begin
    }
    uint32_t node_block_number = (*--rnode->iterator).value;
    for (auto node = rnode.base(); node != nodes_.end(); ++node) {
      *node = {allocator_->LoadAllocatorBlock(node_block_number)};
      node->iterator = node->node->end();
      node_block_number = (*--node->iterator).value;
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

  bool operator==(const EPTreeIterator& other) const { return nodes_.back().iterator == other.nodes_.back().iterator; }

  std::vector<node_info>& nodes() { return nodes_; };

  // TODO: Never should be empty list
  bool is_begin() const {
    return std::ranges::all_of(nodes_, [](const node_info& node) { return node.iterator.is_begin(); });
  }
  bool is_end() const { return nodes_.back().iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;

  std::vector<node_info> nodes_;
};
static_assert(std::bidirectional_iterator<EPTreeIterator>);

class EPTree : public EPTreeBlock {
 public:
  using iterator = EPTreeIterator;
  using const_iterator = iterator;  // todo
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  EPTree(FreeBlocksAllocator* allocator) : EPTreeBlock(allocator->root_block()), allocator_(allocator) {}

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>
  Iterator tbegin() const {
    std::vector<typename Iterator::node_info> nodes;
    nodes.reserve(tree_header()->depth.value());
    assert(tree_header()->depth.value() >= 1);
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      typename Iterator::node_info node{i == 0 ? block()
                                               : allocator_->LoadAllocatorBlock((*nodes.back().iterator).value)};
      node.iterator = node.node->begin();
      nodes.push_back(std::move(node));
      // Each RTree in EPTree must have leafs
      assert(!nodes.back().iterator.is_end());
    }
    return Iterator(allocator_, std::move(nodes));
  }
  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>
  Iterator tend() const {
    std::vector<typename Iterator::node_info> nodes;
    nodes.reserve(tree_header()->depth.value());
    assert(tree_header()->depth.value() >= 1);
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      typename Iterator::node_info node{i == 0 ? block()
                                               : allocator_->LoadAllocatorBlock((*--nodes.back().iterator).value)};
      node.iterator = node.node->end();
      nodes.push_back(std::move(node));
      // Each RTree in EPTree must have leafs
      assert(!nodes.back().iterator.is_begin());
    }
    return Iterator(allocator_, std::move(nodes));
  }
  template <typename ReverseIterator>
    requires std::is_same_v<ReverseIterator, reverse_iterator> ||
             std::is_same_v<ReverseIterator, const_reverse_iterator>
  ReverseIterator tbegin() const {
    if constexpr (std::is_same_v<ReverseIterator, reverse_iterator>) {
      return ReverseIterator{tend<iterator>()};
    } else {
      return ReverseIterator{tend<const_iterator>()};
    }
  }

  template <typename ReverseIterator>
    requires std::is_same_v<ReverseIterator, reverse_iterator> ||
             std::is_same_v<ReverseIterator, const_reverse_iterator>
  ReverseIterator tend() const {
    if constexpr (std::is_same_v<ReverseIterator, reverse_iterator>) {
      return ReverseIterator{tbegin<iterator>()};
    } else {
      return ReverseIterator{tbegin<const_iterator>()};
    }
  }

  auto begin() { return tbegin<iterator>(); }
  auto end() { return tend<iterator>(); }
  auto begin() const { return tbegin<const_iterator>(); }
  auto end() const { return tend<const_iterator>(); }

  auto rbegin() { return tbegin<reverse_iterator>(); }
  auto rend() { return tend<reverse_iterator>(); }
  auto rbegin() const { return tbegin<const_reverse_iterator>(); }
  auto rend() const { return tend<const_reverse_iterator>(); }

  auto cbegin() const { return begin(); }
  auto cend() const { return end(); }

  template <typename Iterator>
    requires std::is_same_v<Iterator, iterator> || std::is_same_v<Iterator, const_iterator>
  Iterator tfind(key_type key, bool exact_match = false) const {
    std::vector<typename Iterator::node_info> nodes;
    nodes.reserve(tree_header()->depth.value());
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      typename Iterator::node_info node{i == 0 ? block()
                                               : allocator_->LoadAllocatorBlock((*nodes.back().iterator).value)};
      node.iterator = node.node->find(key, exact_match && i + 1 == tree_header()->depth.value());
      nodes.push_back(std::move(node));
      assert(!nodes.back().iterator.is_end());
    }
    return {allocator_, std::move(nodes)};
  }

  template <typename ReverseIterator>
    requires std::is_same_v<ReverseIterator, reverse_iterator>
  ReverseIterator tfind(key_type key, bool exact_match = false) const {
    auto res = tfind<iterator>(key, exact_match);
    if (res.is_end())
      return ReverseIterator{res};
    else
      return ReverseIterator{++res};
  }

  template <typename ReverseIterator>
    requires(!std::is_same_v<reverse_iterator, const_reverse_iterator> &&
             std::is_same_v<ReverseIterator, const_reverse_iterator>)  // TODO
  ReverseIterator tfind(key_type key, bool exact_match = false) const {
    auto res = tfind<const_iterator>(key, exact_match);
    if (res.is_end())
      return ReverseIterator{res};
    else
      return ReverseIterator{++res};
  }

  auto find(key_type key, bool exact_match = false) { return tfind<iterator>(key, exact_match); }
  auto rfind(key_type key, bool exact_match = false) { return tfind<reverse_iterator>(key, exact_match); }
  auto find(key_type key, bool exact_match = false) const { return tfind<const_iterator>(key, exact_match); }
  auto rfind(key_type key, bool exact_match = false) const { return tfind<const_reverse_iterator>(key, exact_match); }

  bool insert(const iterator::value_type& key_value) {
    auto it = find(key_value.key);
    if (it.nodes().back().node->insert(key_value)) {
      return true;
    }
    // Need to grow
    std::vector<FreeBlocksExtentInfo> allocated_extents;
    iterator::value_type key_val_to_add = key_value;
    for (auto& [node_level, _] : std::views::reverse(it.nodes())) {
      auto depth = node_level->tree_header()->depth.value();
      // Where to split the level
      auto split_point = node_level->middle();
      auto split_point_key = (*split_point).key;
      // Alloc new right side tree
      auto right_block_number = AllocBlockForTree(node_level->block()->BlockNumber(), allocated_extents);
      RTree new_right(allocator_->LoadAllocatorBlock(right_block_number, /*new_block=*/true));
      if (depth == tree_header()->depth.value()) {
        // This is the root split it to two new trees
        if (depth == 3) {
          // can't grow anymore
          return false;
        }
        // We need a new left side too
        auto left_block_number = AllocBlockForTree(node_level->block()->BlockNumber(), allocated_extents);
        RTree new_left(allocator_->LoadAllocatorBlock(left_block_number, /*new_block=*/true));
        new_right.Init(depth);
        new_left.Init(depth);
        node_level->split(new_left, new_right, split_point);
        // Reset the root
        node_level->Init(depth + 1);
        node_level->insert({0, left_block_number});
        node_level->insert({split_point_key, right_block_number});
      } else {
        new_right.Init(depth);
        node_level->split(new_right, split_point);
      }
      if (node_level->insert(key_val_to_add))
        break;
      key_val_to_add = iterator::value_type{split_point_key, right_block_number};
    }
    for (auto extent : allocated_extents)
      allocator_->RemoveFreeBlocksExtent(extent);
    return true;
  }

  bool insert(const RTree::iterator& it_start, const RTree::iterator& it_end) {
    for (const auto& val : std::ranges::subrange(it_start, it_end)) {
      if (!insert(val))
        return false;
    }
    return true;
  }

  void erase(iterator pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
    // Erase from each node
    for (auto& [node_level, node_it] : std::views::reverse(pos.nodes())) {
      node_level->erase(node_it);
      if (node_level->header() == &tree_header()->current_tree) {
        // This is root, don't delete anymore
        return;
      } else if (node_level->header()->items_count.value() > 1) {
        return;
      } else if (!node_level->empty()) {
        break;
      }
      // node is empty, delete parent too
      blocks_to_delete.push_back({node_level->block()->BlockNumber(), 1});
    }
  }

  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
    auto it = find(key, true);
    if (it.is_end())
      return false;
    erase(it, blocks_to_delete);
    return true;
  }

 private:
  uint32_t AllocBlockForTree(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated) {
    auto block_number = allocator_->AllocFreeBlockFromCache();
    if (block_number)
      return block_number;
    return allocator_->FindSmallestFreeBlockExtent(near, allocated);
  }

  FreeBlocksAllocator* allocator_;
};

// Iterator for specific size
template <typename eptree_node_info_type, typename ftree_node_info_type>
class FreeBlocksTreeBucketIteratorBase {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;

  using value_type = FreeBlocksExtent;
  using ref_type = FreeBlocksExtentRef;

  using const_reference = value_type;

  using reference =
      std::conditional<is_const_iterator_info<FTree, ftree_node_info_type>, const_reference, ref_type>::type;

  using eptree_node_info = eptree_node_info_type;
  using ftree_node_info = ftree_node_info_type;

  FreeBlocksTreeBucketIteratorBase() = default;
  FreeBlocksTreeBucketIteratorBase(FreeBlocksAllocator* allocator,
                                   size_t block_size_index,
                                   eptree_node_info eptree,
                                   ftree_node_info ftree)
      : allocator_(allocator),
        block_size_index_(block_size_index),
        eptree_(std::move(eptree)),
        ftree_(std::move(ftree)) {}

  reference operator*() const {
    auto key_val = *ftree_.iterator;
    return {key_val.key, key_val.value, block_size_index_};
  }

  FreeBlocksTreeBucketIteratorBase& operator++() {
    assert(!is_end());
    ++ftree_.iterator;
    while (ftree_.iterator.is_end()) {
      if ((++eptree_.iterator).is_end()) {
        --eptree_.iterator;
        return *this;  // end
      }

      ftree_ = {{allocator_->LoadAllocatorBlock((*eptree_.iterator).value), block_size_index_}};
      ftree_.iterator = ftree_.node->begin();
    }
    return *this;
  }

  FreeBlocksTreeBucketIteratorBase& operator--() {
    assert(!is_begin());
    while (ftree_.iterator.is_begin()) {
      if (eptree_.iterator.is_begin()) {
        return *this;  // begin
      }

      ftree_ = {{allocator_->LoadAllocatorBlock((*--eptree_.iterator).value), block_size_index_}};
      ftree_.iterator = ftree_.node->end();
    }
    --ftree_.iterator;
    return *this;
  }

  FreeBlocksTreeBucketIteratorBase operator++(int) {
    FreeBlocksTreeBucketIteratorBase tmp(*this);
    ++(*this);
    return tmp;
  }

  FreeBlocksTreeBucketIteratorBase operator--(int) {
    FreeBlocksTreeBucketIteratorBase tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const FreeBlocksTreeBucketIteratorBase& other) const {
    return ftree_.iterator == other.ftree_.iterator;
  }

  eptree_node_info& eptree() { return eptree_; }

  ftree_node_info& ftree() { return ftree_; }

  bool is_begin() const { return eptree_.iterator.is_begin() && ftree_.iterator.is_begin(); }
  bool is_end() const { return ftree_.iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;
  size_t block_size_index_;

  eptree_node_info eptree_;
  ftree_node_info ftree_;
};
using FreeBlocksTreeBucketIterator =
    FreeBlocksTreeBucketIteratorBase<node_iterator_info<EPTree>, node_iterator_info<FTree>>;
using FreeBlocksTreeConstBucketIterator =
    FreeBlocksTreeBucketIteratorBase<node_const_iterator_info<EPTree>, node_const_iterator_info<FTree>>;

static_assert(std::bidirectional_iterator<FreeBlocksTreeBucketIterator>);
static_assert(std::bidirectional_iterator<FreeBlocksTreeConstBucketIterator>);

class FreeBlocksTreeBucket {
 public:
  using iterator = FreeBlocksTreeBucketIterator;
  using const_iterator = FreeBlocksTreeConstBucketIterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  FreeBlocksTreeBucket(FreeBlocksAllocator* allocator, size_t block_size_index)
      : allocator_(allocator), block_size_index_(block_size_index) {}

  iterator begin() const {
    iterator::eptree_node_info eptree{{allocator_}};
    eptree.iterator = eptree.node->begin();
    assert(eptree.iterator != eptree.node->end());
    iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value), block_size_index_}};
    ftree.iterator = ftree.node->begin();
    return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
  }

  iterator end() const {
    iterator::eptree_node_info eptree{{allocator_}};
    eptree.iterator = eptree.node->end();
    assert(eptree.iterator != eptree.node->begin());
    --eptree.iterator;  // EPTree size should always be >= 1
    iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value), block_size_index_}};
    ftree.iterator = ftree.node->end();
    return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
  }

  // Note: find may return an empty FTree iterator, which isn't compatible with other functinalities
  // TODO: Remove this class at all? move this logic somewhere else
  // TODO: Fix this
  iterator find(key_type key, bool exact_match = true) const {
    iterator::eptree_node_info eptree{{allocator_}};
    eptree.iterator = eptree.node->find(key, exact_match);
    if (exact_match && eptree.iterator == eptree.node->end())
      return end();
    iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value), block_size_index_}};
    ftree.iterator = ftree.node->find(key, exact_match);
    // TODO: If empty iterator back over eptree to find the last one
    return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
  }

  bool update(const iterator::value_type& key_val) {
    auto pos = find(key_val.key);
    if (pos.is_end()) {
      return false;
    }
    (*pos).value = key_val.value;

    return true;
  }

  bool insert(FTree::iterator::value_type key_val) {
    // TODO: If implementing the thing in find, we will have to specificlly find eptree here
    auto pos = find(key_val.key, false);
    if (!pos.is_end() && (*pos).key == key_val.key) {
      // already in tree
      return false;
    }
    return insert(pos, key_val);
  }

  bool insert(iterator& pos, FTree::iterator::value_type key_val) {
    if (pos.ftree().node->insert(key_val)) {
      return true;
    }

    auto old_block = pos.ftree().node->block()->CreateInMemoryClone();
    FTrees old_ftrees{old_block};

    auto left_block = pos.ftree().node->block();
    std::fill(left_block->mutable_data().begin(), left_block->mutable_data().end(), std::byte{0});

    std::optional<FreeBlocksExtentInfo> allocated_extent;
    auto right_block_number = allocator_->AllocFreeBlockFromCache();
    if (!right_block_number) {
      // Just get a block from the current FTree
      for (const auto& ftree : old_ftrees.ftrees()) {
        if (ftree.size() > 0) {
          auto first = ftree.begin();
          *allocated_extent = FreeBlocksExtent{(*first).key, (*first).value, ftree.index()};
          break;
        }
      }
      if (!allocated_extent)
        return false;
      allocated_extent->blocks_count = 1;
      right_block_number = allocated_extent->block_number;
    }
    auto right_block = allocator_->LoadAllocatorBlock(right_block_number, true);
    FTreesBlock(left_block).Init();
    FTreesBlock(right_block).Init();

    FTrees left_ftrees{left_block}, right_ftrees{right_block};

    key_type split_point_key;
    old_ftrees.split(left_ftrees, right_ftrees, split_point_key);
    if (key_val.key < split_point_key)
      left_ftrees.ftrees()[block_size_index_].insert(key_val);
    else
      right_ftrees.ftrees()[block_size_index_].insert(key_val);
    if (!pos.eptree().node->insert({split_point_key, right_block_number}))
      return false;
    if (allocated_extent)
      allocator_->RemoveFreeBlocksExtent(*allocated_extent);
    return true;
  }

  void erase(iterator pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
    pos.ftree().node->erase(pos.ftree().iterator);
    if (!pos.ftree().node->empty() || !(*pos.eptree().iterator).key) {
      return;
    }
    if (FTrees(pos.ftree().node->block()).empty()) {
      // The FTRee is the first block for deletion
      blocks_to_delete.push_back({(*pos.eptree().iterator).value, 1});
      pos.eptree().node->erase(pos.eptree().iterator, blocks_to_delete);
    }
  }

  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
    auto it = find(key, true);
    if (it == end())
      return false;
    erase(it, blocks_to_delete);
    return true;
  }

 private:
  FreeBlocksAllocator* allocator_;

  size_t block_size_index_;
};

template <typename eptree_node_info_type, typename ftrees_node_info_type>
class FreeBlocksTreeIteratorBase {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;
  using value_type = typename FTrees::iterator::value_type;

  using const_reference = value_type;
  using reference = const_reference;
  using pointer = const_reference*;

  using eptree_node_info = eptree_node_info_type;
  using ftrees_node_info = ftrees_node_info_type;

  FreeBlocksTreeIteratorBase() = default;
  FreeBlocksTreeIteratorBase(FreeBlocksAllocator* allocator, eptree_node_info eptree, ftrees_node_info ftrees)
      : allocator_(allocator), eptree_(std::move(eptree)), ftrees_(std::move(ftrees)) {}

  reference operator*() const { return *ftrees_.iterator; }

  FreeBlocksTreeIteratorBase& operator++() {
    assert(!is_end());
    ++ftrees_.iterator;
    // support empty ftrees?
    while (ftrees_.iterator.is_end()) {
      if ((++eptree_.iterator).is_end()) {
        --eptree_.iterator;
        return *this;  // end
      }

      ftrees_ = {allocator_->LoadAllocatorBlock((*eptree_.iterator).value)};
      if constexpr (is_reverse_iterator_info<FTrees, ftrees_node_info>) {
        ftrees_.iterator = ftrees_.node->rbegin();
      } else {
        ftrees_.iterator = ftrees_.node->begin();
      }
    }
    return *this;
  }

  FreeBlocksTreeIteratorBase operator++(int) {
    FreeBlocksTreeIteratorBase tmp(*this);
    ++(*this);
    return tmp;
  }

  bool operator==(const FreeBlocksTreeIteratorBase& other) const { return ftrees_.iterator == other.ftrees_.iterator; }

  eptree_node_info& eptree() { return eptree_; }

  ftrees_node_info& ftrees() { return ftrees_; }

  bool is_begin() const { return eptree_.iterator.is_begin() && ftrees_.iterator.is_begin(); }
  bool is_end() const { return ftrees_.iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;

  eptree_node_info eptree_;
  ftrees_node_info ftrees_;
};

using FreeBlocksTreeForwardIterator =
    FreeBlocksTreeIteratorBase<node_iterator_info<EPTree>, node_iterator_info<FTrees>>;
using FreeBlocksTreeBackwardIterator =
    FreeBlocksTreeIteratorBase<node_reverse_iterator_info<EPTree>, node_reverse_iterator_info<FTrees>>;
using FreeBlocksTreeConstForwardIterator =
    FreeBlocksTreeIteratorBase<node_const_iterator_info<EPTree>, node_const_iterator_info<FTrees>>;
using FreeBlocksTreeConstBackwardIterator =
    FreeBlocksTreeIteratorBase<node_const_reverse_iterator_info<EPTree>, node_const_reverse_iterator_info<FTrees>>;

class FreeBlocksTree {
 public:
  using iterator = FreeBlocksTreeForwardIterator;
  using const_iterator = FreeBlocksTreeConstForwardIterator;
  using reverse_iterator = FreeBlocksTreeBackwardIterator;
  using const_reverse_iterator = FreeBlocksTreeConstBackwardIterator;

  FreeBlocksTree(FreeBlocksAllocator* allocator) : allocator_(allocator) {}

  auto begin() { return tbegin<iterator>(); }
  auto end() { return tend<iterator>(); }
  auto rbegin() { return tbegin<reverse_iterator>(); }
  auto rend() { return tend<reverse_iterator>(); }

  auto begin() const { return tbegin<const_iterator>(); }
  auto end() const { return tend<const_iterator>(); }
  auto rbegin() const { return tbegin<const_reverse_iterator>(); }
  auto rend() const { return tend<const_reverse_iterator>(); }

  auto find(key_type key) { return tfind<iterator>(key); }
  auto rfind(key_type key) { return tfind<reverse_iterator>(key); }
  auto find(key_type key) const { return tfind<const_iterator>(key); }
  auto rfind(key_type key) const { return tfind<const_reverse_iterator>(key); }

 private:
  template <typename Iterator>
  Iterator tbegin() const {
    typename Iterator::eptree_node_info eptree{{allocator_}};
    eptree.iterator = eptree.node->template tbegin<decltype(Iterator::eptree_node_info::iterator)>();
    assert(!eptree.iterator.is_end());
    typename Iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock((*eptree.iterator).value)}};
    ftrees.iterator = ftrees.node->template tbegin<decltype(Iterator::ftrees_node_info::iterator)>();
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }

  template <typename Iterator>
  Iterator tend() const {
    typename Iterator::eptree_node_info eptree{{allocator_}};
    eptree.iterator = eptree.node->template tend<decltype(Iterator::eptree_node_info::iterator)>();
    assert(!eptree.iterator.is_begin());
    --eptree.iterator;  // EPTree size should always be >= 1
    typename Iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock((*eptree.iterator).value)}};
    ftrees.iterator = ftrees.node->template tend<decltype(Iterator::ftrees_node_info::iterator)>();
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }

  template <typename Iterator>
  Iterator tfind(key_type key) const {
    typename Iterator::eptree_node_info eptree{{allocator_}};
    eptree.iterator = eptree.node->template tfind<decltype(Iterator::eptree_node_info::iterator)>(key, false);
    typename Iterator::ftrees_node_info ftrees{{allocator_->LoadAllocatorBlock((*eptree.iterator).value)}};
    ftrees.iterator = ftrees.node->template tfind<decltype(Iterator::ftrees_node_info::iterator)>(key);
    return {allocator_, std::move(eptree), std::move(ftrees)};
  }
  FreeBlocksAllocator* allocator_;
};
