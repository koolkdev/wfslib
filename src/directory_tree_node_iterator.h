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

struct dir_tree_node_ref : public Block::RawDataRef<DirectoryTreeNodeHeader> {
  size_t node_size;

  template <typename LeafValueType>
  static dir_tree_node_ref create(Block* block, uint16_t offset) {
    return {{block, offset}, get_node_size<LeafValueType>(block->get_object<DirectoryTreeNodeHeader>(offset))};
  }

  dir_tree_key_type* mutable_key_ref(size_t index) { return get_key_ref(get_mutable(), index); }
  const dir_tree_key_type* key_ref(size_t index) const {
    return get_key_ref(const_cast<DirectoryTreeNodeHeader*>(get()), index);
  }

  template <typename LeafValueType>
  big_endian_type<dir_tree_value_type>* mutable_value_ref(size_t index) {
    return get_value_ref<LeafValueType>(get_mutable(), index, node_size);
  }
  template <typename LeafValueType>
  const big_endian_type<dir_tree_value_type>* value_ref(size_t index) const {
    return get_value_ref<LeafValueType>(const_cast<DirectoryTreeNodeHeader*>(get()), index, node_size);
  }

  template <typename LeafValueType>
  big_endian_type<LeafValueType>* mutable_leaf_value_ref() {
    return get_leaf_value_ref<LeafValueType>(get_mutable(), node_size);
  }
  template <typename LeafValueType>
  const big_endian_type<LeafValueType>* leaf_value_ref() const {
    return const_cast<dir_tree_node_ref*>(this)->get_leaf_value_ref<LeafValueType>(
        const_cast<DirectoryTreeNodeHeader*>(get()), node_size);
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

  template <typename LeafValueType>
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

  template <typename LeafValueType>
  big_endian_type<LeafValueType>* get_leaf_value_ref(DirectoryTreeNodeHeader* node, size_t node_size) {
    assert(has_leaf_value(node));
    return reinterpret_cast<big_endian_type<LeafValueType>*>(get_value_ref<LeafValueType>(node, 0, node_size));
  }

  static bool has_leaf_value(const DirectoryTreeNodeHeader* node) {
    return *get_key_ref(const_cast<DirectoryTreeNodeHeader*>(node), 0) == '\0';
  }

  template <typename LeafValueType>
  static size_t get_node_size(const DirectoryTreeNodeHeader* node) {
    return calc_node_size<LeafValueType>(node->prefix_length.value(), node->keys_count.value(), has_leaf_value(node));
  }
};

struct dir_tree_node_item_ref_base : dir_tree_node_ref {
  size_t index;
  auto operator<=>(const dir_tree_node_item_ref_base& other) const {
    if (const auto res = dir_tree_node_ref::operator<=>(other); res != 0)
      return res;
    return index <=> other.index;
  }
  bool operator==(const dir_tree_node_item_ref_base& other) const {
    return dir_tree_node_ref::operator==(other) && index == other.index;
  }
};

struct dir_tree_node_prefix_ref : protected dir_tree_node_item_ref_base {
  operator std::string() const { return {prefix().begin(), prefix().end()}; }
  operator std::string_view() const { return prefix(); }

  dir_tree_node_prefix_ref& operator=(const dir_tree_node_prefix_ref& other) {
    *this = static_cast<std::string_view>(other);
    return *this;
  }

  dir_tree_node_prefix_ref& operator=(std::string_view val) {
    assert(get()->prefix_length.value() == val.size());
    std::copy(val.begin(), val.end(), mutable_prefix().begin());
    return *this;
  }

  auto operator<=>(const dir_tree_node_prefix_ref& other) const {
    if (*this == other) {
      return 0;
    }
    if (std::ranges::lexicographical_compare(static_cast<std::string_view>(*this),
                                             static_cast<std::string_view>(other))) {
      return -1;
    }
    return 1;
  }
  bool operator==(const dir_tree_node_prefix_ref& other) const {
    return std::ranges::equal(static_cast<std::string_view>(*this), static_cast<std::string_view>(other));
  }
};

struct dir_tree_node_key_ref : protected dir_tree_node_item_ref_base {
  operator dir_tree_key_type() const { return *key_ref(index); }

  dir_tree_node_key_ref& operator=(const dir_tree_node_key_ref& other) {
    *this = static_cast<dir_tree_key_type>(other);
    return *this;
  }

  dir_tree_node_key_ref& operator=(dir_tree_key_type val) {
    *mutable_key_ref(index) = val;
    return *this;
  }

  auto operator<=>(const dir_tree_node_key_ref& other) const {
    return static_cast<dir_tree_key_type>(*this) <=> static_cast<dir_tree_key_type>(other);
  }
  bool operator==(const dir_tree_node_key_ref& other) const {
    return static_cast<dir_tree_key_type>(*this) == static_cast<dir_tree_key_type>(other);
  }
};

template <typename LeafValueType>
struct dir_tree_node_value_ref : protected dir_tree_node_item_ref_base {
  dir_tree_node_value_ref(const dir_tree_node_value_ref<LeafValueType>&) = default;

  operator dir_tree_value_type() const { return value_ref<LeafValueType>(index)->value(); }

  operator LeafValueType() const
    requires(!std::same_as<dir_tree_value_type, LeafValueType>)
  {
    if (index == 0 && has_leaf_value())
      return leaf_value_ref<LeafValueType>()->value();
    else
      return static_cast<dir_tree_value_type>(*this);
  }

  dir_tree_node_value_ref& operator=(const dir_tree_node_value_ref& other) {
    *this = static_cast<dir_tree_value_type>(other);
    return *this;
  }

  dir_tree_node_value_ref& operator=(dir_tree_value_type val) {
    *mutable_value_ref<LeafValueType>(index) = val;
    return *this;
  }

  dir_tree_node_value_ref& operator=(LeafValueType val)
    requires(!std::same_as<dir_tree_value_type, LeafValueType>)
  {
    if (index == 0 && has_leaf_value())
      *mutable_leaf_value_ref<LeafValueType>() = val;
    else
      *this = static_cast<dir_tree_value_type>(val);
    return *this;
  }

  auto operator<=>(const dir_tree_node_value_ref& other) const {
    return static_cast<dir_tree_value_type>(*this) <=> static_cast<dir_tree_value_type>(other);
  }
  bool operator==(const dir_tree_node_value_ref& other) const {
    return static_cast<dir_tree_value_type>(*this) == static_cast<dir_tree_value_type>(other);
  }
};

struct dir_tree_node_item {
  dir_tree_key_type key;
  dir_tree_value_max_type value;

  auto operator<=>(const dir_tree_node_item& other) const {
    if (const auto res = key <=> other.key; res != 0)
      return res;
    return value <=> other.value;
  }
  bool operator==(const dir_tree_node_item& other) const { return key == other.key && value == other.value; }
  auto operator<=>(dir_tree_key_type other_key) const { return key <=> other_key; }
  bool operator==(dir_tree_key_type other_key) const { return key == other_key; }
};

template <typename LeafValueType>
struct dir_tree_node_item_ref {
  union {
    dir_tree_node_item_ref_base _base;
    dir_tree_node_key_ref key;
    dir_tree_node_value_ref<LeafValueType> value;
  };

  dir_tree_node_item_ref& operator=(const dir_tree_node_item_ref& other) {
    key = other.key;
    value = other.value;
    return *this;
  }

  dir_tree_node_item_ref& operator=(const dir_tree_node_item& val) {
    key = val.key;
    value = val.value;
    return *this;
  }

  dir_tree_node_item_ref& operator=(dir_tree_key_type val) {
    key = val;
    return *this;
  }

  operator dir_tree_node_item() const { return {key, value}; }

  auto operator<=>(const dir_tree_node_item_ref& other) const {
    return static_cast<dir_tree_node_item>(*this) <=> static_cast<dir_tree_node_item>(other);
  }
  bool operator==(const dir_tree_node_item_ref& other) const {
    return static_cast<dir_tree_node_item>(*this) == static_cast<dir_tree_node_item>(other);
  }
  auto operator<=>(dir_tree_key_type other_key) const { return static_cast<dir_tree_key_type>(key) <=> other_key; }
  bool operator==(dir_tree_key_type other_key) const { return static_cast<dir_tree_key_type>(key) == other_key; }
};

template <typename LeafValueType>
class DirectoryTreeNodeIterator {
 public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = int;
  using value_type = dir_tree_value_max_type;

  using ref_type = dir_tree_node_item_ref<LeafValueType>;

  using reference = ref_type;

  DirectoryTreeNodeIterator() = default;
  DirectoryTreeNodeIterator(dir_tree_node_ref node, size_t index)
      : DirectoryTreeNodeIterator(dir_tree_node_item_ref_base{node, index}) {}
  DirectoryTreeNodeIterator(dir_tree_node_item_ref_base node_item) : node_item_(node_item) {}

  reference operator*() const { return {node_item_}; }

  DirectoryTreeNodeIterator& operator++() {
    ++node_item_.index;
    return *this;
  }

  DirectoryTreeNodeIterator& operator--() {
    --node_item_.index;
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
    node_item_.index += n;
    return *this;
  }
  DirectoryTreeNodeIterator& operator-=(difference_type n) {
    node_item_.index -= n;
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
    assert(node_item_.get() == other.node_item_.get());
    return static_cast<difference_type>(node_item_.index) - static_cast<difference_type>(other.node_item_.index);
  }

  auto operator<=>(const DirectoryTreeNodeIterator& other) const {
    if (const auto res = node_item_.get() <=> other.node_item_.get(); res != 0)
      return res;
    return node_item_.index <=> other.node_item_.index;
  }

  bool operator==(const DirectoryTreeNodeIterator& other) const { return node_item_ == other.node_item_; }

  const ref_type operator[](difference_type n) const { return *(*this + n); }

  bool is_leaf_value() const { return node_item_.index == 0 && node_item_.has_leaf_value(); }

  bool is_begin() const { return node_item_.index == 0; }
  bool is_end() const { return node_item_.index == node_item_->keys_count.value(); }

 private:
  dir_tree_node_item_ref_base node_item_;
};
