/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstdint>

#include "structs.h"
#include "utils.h"

using dir_tree_key_type = char;
using dir_tree_value_type = uint16_t;
using dir_tree_value_max_type = uint32_t;

using dir_parent_tree_value_type = uint32_t;
using dir_leaf_tree_value_type = uint16_t;

template <typename LeafValueType>
size_t calc_node_size(uint8_t prefix_length, uint8_t keys_count, bool has_leaf_value) {
  auto size = sizeof(DirectoryTreeNodeHeader) + prefix_length +
              keys_count * (sizeof(dir_tree_key_type) + sizeof(dir_tree_value_type));
  if constexpr (sizeof(LeafValueType) != sizeof(dir_tree_value_type)) {
    if (has_leaf_value) {
      size += sizeof(LeafValueType) - sizeof(dir_tree_value_type);
    }
  }
  return align_to_power_of_2(size);
}

// TODO: Merge with tree_utils.h and node_iterator_info
template <typename T>
struct dir_node_iterator_info {
  std::shared_ptr<T> node;
  typename T::iterator iterator;

  dir_node_iterator_info() = default;
  dir_node_iterator_info(const dir_node_iterator_info& other) : node(other.node), iterator(other.iterator) {}
  dir_node_iterator_info(dir_node_iterator_info&& other)
      : node(std::move(other.node)), iterator(std::move(other.iterator)) {}
  dir_node_iterator_info(std::shared_ptr<T> n, typename T::iterator it) : node(std::move(n)), iterator(std::move(it)) {}
  dir_node_iterator_info(const T& n, typename T::iterator it) : node(std::make_shared<T>(n)), iterator(std::move(it)) {}
  dir_node_iterator_info(T&& n, typename T::iterator it)
      : node(std::make_shared<T>(std::move(n))), iterator(std::move(it)) {}
  dir_node_iterator_info(const T& n) : node(std::make_shared<T>(n)) {}
  dir_node_iterator_info(T&& n) : node(std::make_shared<T>(std::move(n))) {}
  dir_node_iterator_info& operator=(const dir_node_iterator_info& other) {
    node = other.node;
    iterator = other.iterator;
    return *this;
  }
};
