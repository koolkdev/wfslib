/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "block.h"
#include "directory_tree_utils.h"
#include "structs.h"

template <typename LeafValueType>
struct dir_tree_node_ref : public Block::RawDataRef<DirectoryTreeNodeHeader> {
  size_t node_size;

  static dir_tree_node_ref create(Block* block, uint16_t offset) {
    return {{block, offset}, get_node_size(block->get_object<DirectoryTreeNodeHeader>(offset))};
  }

  dir_tree_key_type* mutable_key_ref(size_t index) { return get_key_ref(get_mutable(), index); }
  const dir_tree_key_type* key_ref(size_t index) const {
    return get_key_ref(const_cast<DirectoryTreeNodeHeader*>(get()), index);
  }

  big_endian_type<dir_tree_value_type>* mutable_value_ref(size_t index) {
    return get_value_ref(get_mutable(), index, node_size);
  }
  const big_endian_type<dir_tree_value_type>* value_ref(size_t index) const {
    return get_value_ref(const_cast<DirectoryTreeNodeHeader*>(get()), index, node_size);
  }

  big_endian_type<LeafValueType>* mutable_leaf_value_ref() { return get_leaf_value_ref(get_mutable(), node_size); }

  const big_endian_type<LeafValueType>* leaf_value_ref() const {
    return const_cast<dir_tree_node_ref*>(this)->get_leaf_value_ref(const_cast<DirectoryTreeNodeHeader*>(get()),
                                                                    node_size);
  }

  std::string_view prefix() const {
    auto* node = get();
    return {reinterpret_cast<const char*>(node + 1), node->prefix_length.value()};
  }
  std::span<char> mutable_prefix() {
    auto* node = get_mutable();
    return {reinterpret_cast<char*>(node + 1), node->prefix_length.value()};
  }

  bool has_leaf_value() const { return has_leaf_value(get()); }

 private:
  static dir_tree_key_type* get_key_ref(DirectoryTreeNodeHeader* node, size_t index) {
    assert(index < node->keys_count.value());
    return reinterpret_cast<char*>(node + 1) + node->prefix_length.value() + index;
  }

  static big_endian_type<dir_tree_value_type>* get_value_ref(DirectoryTreeNodeHeader* node,
                                                             size_t index,
                                                             size_t node_size) {
    assert(index < node->keys_count.value());
    auto* end = reinterpret_cast<std::byte*>(node) + node_size - sizeof(dir_tree_value_type);
    if constexpr (sizeof(dir_tree_value_type) != sizeof(LeafValueType)) {
      if (has_leaf_value(node)) {
        end -= sizeof(LeafValueType) - sizeof(dir_tree_value_type);
      }
    }
    return reinterpret_cast<big_endian_type<dir_tree_value_type>*>(end) - index;
  }

  static big_endian_type<LeafValueType>* get_leaf_value_ref(DirectoryTreeNodeHeader* node, size_t node_size) {
    assert(has_leaf_value(node));
    return reinterpret_cast<big_endian_type<LeafValueType>*>(get_value_ref(node, 0, node_size));
  }

  static bool has_leaf_value(const DirectoryTreeNodeHeader* node) {
    return *get_key_ref(const_cast<DirectoryTreeNodeHeader*>(node), 0) == '\0';
  }

  static size_t get_node_size(const DirectoryTreeNodeHeader* node) {
    return calc_node_size<LeafValueType>(node->prefix_length.value(), node->keys_count.value(), has_leaf_value(node));
  }
};

template <typename T>
struct dir_tree_node_item {
  dir_tree_key_type key;
  T value;

  auto operator<=>(const dir_tree_node_item& other) const {
    if (const auto res = key <=> other.key; res != 0)
      return res;
    return value <=> other.value;
  }
  bool operator==(const dir_tree_node_item& other) const { return key == other.key && value == other.value; }
  auto operator<=>(dir_tree_key_type other_key) const { return key <=> other_key; }
  bool operator==(dir_tree_key_type other_key) const { return key == other_key; }
};

using dir_tree_parent_node_item = dir_tree_node_item<dir_tree_value_type>;

template <typename LeafValueType>
struct dir_tree_leaf_node_item : public dir_tree_node_item<LeafValueType> {};

template <typename LeafValueType, typename T>
struct dir_tree_node_item_ref {
 public:
  dir_tree_node_item_ref() = default;
  dir_tree_node_item_ref(dir_tree_node_ref<LeafValueType> node, size_t index) : node(node), index(index) {}
  dir_tree_node_item_ref(const dir_tree_node_item_ref& other) = default;

  dir_tree_key_type key() const { return *node.key_ref(index); }
  T value() const {
    if constexpr (!std::same_as<T, dir_tree_value_type>) {
      static_assert(std::same_as<T, LeafValueType>);
      assert(index == 0 && node.has_leaf_value());
      return node.leaf_value_ref()->value();
    } else {
      return node.value_ref(index)->value();
    }
  }

  void set_key(dir_tree_key_type key) { *node.mutable_key_ref(index) = key; }
  void set_value(T value) {
    if constexpr (!std::same_as<T, dir_tree_value_type>) {
      static_assert(std::same_as<T, LeafValueType>);
      assert(index == 0 && node.has_leaf_value());
      *node.mutable_leaf_value_ref() = value;
    } else {
      *node.mutable_value_ref(index) = value;
    }
  }

  dir_tree_node_item_ref& operator=(const dir_tree_node_item_ref& other) {
    set_key(other.key());
    set_value(other.value());
    return *this;
  }

  dir_tree_node_item_ref& operator=(const dir_tree_node_item<T>& val) {
    set_key(val.key);
    set_value(val.value);
    return *this;
  }

  dir_tree_node_item_ref& operator=(dir_tree_key_type val) {
    set_key(val);
    return *this;
  }

  operator dir_tree_node_item<T>() const { return {key(), value()}; }

  auto operator<=>(const dir_tree_node_item_ref& other) const {
    return static_cast<dir_tree_node_item<T>>(*this) <=> static_cast<dir_tree_node_item<T>>(other);
  }
  bool operator==(const dir_tree_node_item_ref& other) const {
    return static_cast<dir_tree_node_item<T>>(*this) == static_cast<dir_tree_node_item<T>>(other);
  }
  auto operator<=>(dir_tree_key_type other_key) const { return key() <=> other_key; }
  bool operator==(dir_tree_key_type other_key) const { return key() == other_key; }

  const dir_tree_node_ref<LeafValueType>& get_node() const { return node; }

 private:
  dir_tree_node_ref<LeafValueType> node;
  size_t index;
};

template <typename LeafValueType>
struct dir_tree_parent_node_item_ref : public dir_tree_node_item_ref<LeafValueType, dir_tree_value_type> {
  dir_tree_parent_node_item_ref() = default;
  dir_tree_parent_node_item_ref(dir_tree_node_ref<LeafValueType> node, size_t index)
      : dir_tree_node_item_ref<LeafValueType, dir_tree_value_type>(node, index) {}
  dir_tree_parent_node_item_ref(const dir_tree_parent_node_item_ref& other) = default;
};

template <typename LeafValueType>
struct dir_tree_leaf_node_item_ref : public dir_tree_node_item_ref<LeafValueType, LeafValueType> {
  dir_tree_leaf_node_item_ref() = default;
  dir_tree_leaf_node_item_ref(dir_tree_node_ref<LeafValueType> node, size_t index)
      : dir_tree_node_item_ref<LeafValueType, LeafValueType>(node, index) {}
  dir_tree_leaf_node_item_ref(const dir_tree_leaf_node_item_ref& other) = default;
};

template <typename LeafValueType>
class DirectoryTreeNodeIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = dir_tree_parent_node_item;

  using ref_type = dir_tree_parent_node_item_ref<LeafValueType>;

  using reference = ref_type;

  DirectoryTreeNodeIterator() = default;
  DirectoryTreeNodeIterator(dir_tree_node_ref<LeafValueType> node, size_t index) : node_(node), index_(index) {}

  reference operator*() const { return {node_, index_}; }

  DirectoryTreeNodeIterator& operator++() {
    ++index_;
    return *this;
  }

  DirectoryTreeNodeIterator& operator--() {
    --index_;
    return *this;
  }

  DirectoryTreeNodeIterator operator++(int) {
    DirectoryTreeNodeIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  DirectoryTreeNodeIterator operator--(int) {
    DirectoryTreeNodeIterator tmp(*this);
    --(*this);
    return tmp;
  }
  DirectoryTreeNodeIterator& operator+=(difference_type n) {
    index_ += n;
    return *this;
  }
  DirectoryTreeNodeIterator& operator-=(difference_type n) {
    index_ -= n;
    return *this;
  }

  DirectoryTreeNodeIterator operator+(difference_type n) const {
    DirectoryTreeNodeIterator tmp(*this);
    tmp += n;
    return tmp;
  }

  DirectoryTreeNodeIterator operator-(difference_type n) const {
    DirectoryTreeNodeIterator tmp(*this);
    tmp -= n;
    return tmp;
  }

  difference_type operator-(const DirectoryTreeNodeIterator& other) const {
    assert(node_.get() == other.node_.get());
    return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
  }

  auto operator<=>(const DirectoryTreeNodeIterator& other) const {
    if (const auto res = node_.get() <=> other.node_.get(); res != 0)
      return res;
    return index_ <=> other.index_;
  }

  bool operator==(const DirectoryTreeNodeIterator& other) const {
    return node_ == other.node_ && index_ == other.index_;
  }

  const ref_type operator[](difference_type n) const { return *(*this + n); }

  bool is_begin() const { return index_ == (node_.has_leaf_value() ? 1 : 0); }
  bool is_end() const { return index_ == node_->keys_count.value(); }

 private:
  dir_tree_node_ref<LeafValueType> node_;
  size_t index_;
};
