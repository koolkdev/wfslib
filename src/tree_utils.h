/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <type_traits>

#include "block.h"
#include "structs.h"
#include "utils.h"

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
concept is_parent_node_details =
    is_node_details<T> && node_keys_capacity<T>::value + 1 == node_values_capacity<T>::value;
template <typename T>
concept is_leaf_node_details = is_node_details<T> && node_keys_capacity<T>::value == node_values_capacity<T>::value;

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

static_assert(std::is_same_v<uint16_t, node_value_type<PTreeNode_details>::type>);
static_assert(node_keys_capacity<PTreeNode_details>::value == 5);
static_assert(node_values_capacity<PTreeNode_details>::value == 6);
static_assert(std::is_same_v<uint32_t, node_value_type<RTreeLeaf_details>::type>);
static_assert(node_keys_capacity<RTreeLeaf_details>::value == 4);
static_assert(node_values_capacity<RTreeLeaf_details>::value == 4);
static_assert(std::is_same_v<nibble, node_value_type<FTreeLeaf_details>::type>);
static_assert(node_keys_capacity<FTreeLeaf_details>::value == 7);
static_assert(node_values_capacity<FTreeLeaf_details>::value == 7);

template <typename T>
struct node_iterator_info {
  T node;
  typename T::iterator iterator{};
};

template <is_node_details T>
struct node_ref : public Block::RawDataRef<T> {};
