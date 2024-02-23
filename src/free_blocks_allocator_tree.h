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

#include "heap_allocator.h"
#include "structs.h"

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

template <has_keys T>
struct node_access_key {
  static uint32_t get(const T& node, size_t i) { return node.keys[i].value(); }
};
template <size_t index, has_keys T>
  requires(index < node_keys_capacity<T>::value)
uint32_t node_get_key(const T& node) {
  return node_access_key<T>::get(node, index);
}
template <has_keys T>
uint32_t node_get_key(const T& node, size_t index) {
  assert(index < node_keys_capacity<T>::value);
  return node_access_key<T>::get(node, index);
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
};
template <has_nibble_values T>
struct node_access_value<T> {
  static node_value_type<T>::type get(const T& node, size_t i) {
    return static_cast<nibble>((node.values.value() >> (4 * i)) & 0xf);
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

static_assert(std::is_same_v<uint16_t, node_value_type<RTreeNode_details>::type>);
static_assert(node_keys_capacity<RTreeNode_details>::value == 5);
static_assert(node_values_capacity<RTreeNode_details>::value == 6);
static_assert(std::is_same_v<uint32_t, node_value_type<RTreeLeaf_details>::type>);
static_assert(node_keys_capacity<RTreeLeaf_details>::value == 4);
static_assert(node_values_capacity<RTreeLeaf_details>::value == 4);
static_assert(std::is_same_v<nibble, node_value_type<FTreeLeaf_details>::type>);
static_assert(node_keys_capacity<FTreeLeaf_details>::value == 7);
static_assert(node_values_capacity<FTreeLeaf_details>::value == 7);

template <is_node_details T>
class PTreeNodeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::pair<uint32_t, typename node_value_type<T>::type>;

  PTreeNodeIterator(const T* node, size_t index, size_t size) : node(node), index(index), size(size) {}

  value_type operator*()
    requires is_parent_node_details<T>
  {
    assert(index < size);
    return value_type{index == 0 ? 0 : node_get_key(*node, index - 1), node_get_value(*node, index)};
  }
  value_type operator*()
    requires is_leaf_node_details<T>
  {
    assert(index < size);
    return value_type(node_get_key(*node, index), node_get_value(*node, index));
  }

  PTreeNodeIterator<T>& operator++() {
    ++index;
    return *this;
  }

  PTreeNodeIterator<T>& operator--() {
    --index;
    return *this;
  }

  PTreeNodeIterator<T> operator++(int) {
    PTreeNodeIterator<T> tmp(*this);
    ++(*this);
    return tmp;
  }

  PTreeNodeIterator<T> operator--(int) {
    PTreeNodeIterator<T> tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const PTreeNodeIterator<T>& other) const { return node == other.node && index == other.index; }
  bool operator!=(const PTreeNodeIterator<T>& other) const { return !operator==(other); }

 private:
  const T* node;
  size_t index;
  size_t size;
};

template <is_node_details T>
class PTreeNode {
 public:
  using iterator = PTreeNodeIterator<T>;

  PTreeNode(const T* node) : node_(node), size_(node_values_size(*node)) {}

  size_t size() { return size_; }
  iterator begin() { return iterator(node_, 0, size_); }
  iterator end() { return iterator(node_, static_cast<int>(size_), size_); }

 private:
  const T* node_;
  size_t size_;
};

template <typename T, typename U>
concept nodes_allocator_methods = requires(T& allocator, U* node_type) {
                                    { allocator.template get_object<U>(uint16_t{0}) } -> std::same_as<U*>;
                                    { allocator.template Alloc<U>(uint16_t{0}) } -> std::same_as<U*>;
                                    allocator.template Free<U>(node_type, uint16_t{0});
                                  } && requires(const T& allocator) {
                                         { allocator.template get_object<U>(uint16_t{0}) } -> std::same_as<const U*>;
                                       };
template <typename T>
concept nodes_allocator_construct = std::constructible_from<T, std::shared_ptr<MetadataBlock>>;

template <typename T, typename U>
concept nodes_allocator = nodes_allocator_methods<T, U> && nodes_allocator_construct<T>;

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename Allocator>
  requires nodes_allocator<Allocator, ParentNodeDetails> && nodes_allocator<Allocator, LeafNodeDetails>
class PTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::pair<uint32_t, typename node_value_type<LeafNodeDetails>::type>;
  using iterator_type = PTreeIterator<ParentNodeDetails, LeafNodeDetails, Allocator>;

  template <typename T>
  using node_iterator_info = std::pair<PTreeNode<T>, typename PTreeNode<T>::iterator>;

  PTreeIterator(const Allocator* allocator,
                std::vector<node_iterator_info<ParentNodeDetails>> parents,
                std::optional<node_iterator_info<LeafNodeDetails>> leaf)
      : allocator_(allocator), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

  value_type operator*() { return *leaf_->second; }

  iterator_type& operator++() {
    if (++leaf_->second == leaf_->first.end()) {
      auto parent = parents_.end();
      if (parent == parents_.begin())
        return *this;  // end
      for (--parent; ++parent->second == parent->first.end(); --parent) {
        if (parent == parents_.begin())
          return *this;  // end
      }
      uint16_t node_offset = (*parent->second).second;  // TODO: by ref
      for (++parent; parent != parents_.end(); ++parent) {
        parent->first = {GetParentNodeData(node_offset)};
        parent->second = parent->first.begin();
        node_offset = (*parent->second).second;  // TODO: by ref
      }
      leaf_->first = {GetLeafNodeData(node_offset)};
      leaf_->second = leaf_->first.begin();
    }
    return *this;
  }

  iterator_type& operator--() {
    if (leaf_->second == leaf_->first.begin()) {
      auto parent = parents_.end();
      if (parent == parents_.begin())
        return *this;  // begin
      for (--parent; ++parent->second == parent->first.begin(); --parent) {
        if (parent == parents_.begin())
          return *this;  // begin
      }
      --parent->second;
      uint16_t node_offset = (*parent->second).second;  // TODO: by ref
      for (++parent; parent != parents_.end(); ++parent) {
        parent->first = {GetParentNodeData(node_offset)};
        parent->second = parent->first.end();
        --parent->second;
        node_offset = (*parent->second).second;  // TODO: by ref
      }
      leaf_->first = {GetLeafNodeData(node_offset)};
      leaf_->second = leaf_->first.end();
    }
    --leaf_->second;
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
    return leaf_->second == other.leaf_->second;
  }
  bool operator!=(const iterator_type& other) const { return !operator==(other); }

 private:
  const ParentNodeDetails* GetParentNodeData(uint16_t offset) const {
    return allocator_->template get_object<ParentNodeDetails>(offset);
  }
  const LeafNodeDetails* GetLeafNodeData(uint16_t offset) const {
    return allocator_->template get_object<LeafNodeDetails>(offset);
  }

  const Allocator* allocator_;
  std::vector<node_iterator_info<ParentNodeDetails>> parents_;
  std::optional<node_iterator_info<LeafNodeDetails>> leaf_;
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename Allocator>
  requires nodes_allocator<Allocator, ParentNodeDetails> && nodes_allocator<Allocator, LeafNodeDetails>
class PTree : public Allocator {
 public:
  using iterator = PTreeIterator<ParentNodeDetails, LeafNodeDetails, Allocator>;
  using const_iterator = const iterator;

  template <typename T>
  using node_iterator_info = std::pair<PTreeNode<T>, typename PTreeNode<T>::iterator>;

  PTree(std::shared_ptr<MetadataBlock> block) : Allocator(std::move(block)) {}

  virtual PTreeHeader* header() = 0;
  virtual const PTreeHeader* header() const = 0;

  size_t size() const { return header()->items_count.value(); }
  iterator begin() const {
    if (size() == 0)
      return iterator(this, {}, std::nullopt);
    std::vector<node_iterator_info<ParentNodeDetails>> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      PTreeNode<ParentNodeDetails> node{GetParentNodeData(node_offset)};
      parents.push_back({node, node.begin()});
      node_offset = (*parents.back().second).second;  // TODO: by ref
    }
    PTreeNode<LeafNodeDetails> leaf{GetLeafNodeData(node_offset)};
    return iterator(this, std::move(parents), {{leaf, leaf.begin()}});
  }
  iterator end() const {
    if (size() == 0)
      return iterator(this, {}, std::nullopt);
    std::vector<node_iterator_info<ParentNodeDetails>> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      PTreeNode<ParentNodeDetails> node{GetParentNodeData(node_offset)};
      parents.push_back({node, node.end()});
      auto prev = node.end();
      node_offset = (*--prev).second;  // TODO: by ref
    }
    PTreeNode<LeafNodeDetails> leaf{GetLeafNodeData(node_offset)};
    return iterator(this, std::move(parents), {{leaf, leaf.end()}});
  }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

 private:
  const ParentNodeDetails* GetParentNodeData(uint16_t offset) const {
    return this->template get_object<ParentNodeDetails>(offset);
  }
  const LeafNodeDetails* GetLeafNodeData(uint16_t offset) const {
    return this->template get_object<LeafNodeDetails>(offset);
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

  PTreeHeader* header() override { return &tree_header()->current_tree; }
  const PTreeHeader* header() const override { return &tree_header()->current_tree; }
};

class FTree : public PTree<RTreeNode_details, FTreeLeaf_details, FTreesBlock> {
 public:
  FTree(std::shared_ptr<MetadataBlock> block, int block_size)
      : PTree<RTreeNode_details, FTreeLeaf_details, FTreesBlock>(std::move(block)), block_size_(block_size) {}

  PTreeHeader* header() override { return &tree_header()->trees[block_size_]; }
  const PTreeHeader* header() const override { return &tree_header()->trees[block_size_]; }

 private:
  int block_size_;
};

class EPTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = std::pair<uint32_t, std::array<FTree, 7>>;

  using node_iterator_info = std::pair<RTree, typename RTree::iterator>;

  EPTreeIterator(std::vector<node_iterator_info> nodes, std::shared_ptr<Area> area)
      : nodes_(std::move(nodes)), area_(std::move(area)) {}

  template <std::size_t N, std::size_t... Is>
  std::array<FTree, N> CreateFTreeArray(uint32_t block_number, std::index_sequence<Is...>) {
    auto block = throw_if_error(area_->GetMetadataBlock(block_number));
    return {{FTree(block, Is)...}};
  }

  value_type operator*() {
    const auto& [key, block_number] = *nodes_.back().second;
    return {key, CreateFTreeArray<7>(block_number, std::make_index_sequence<7>())};
  }

  EPTreeIterator& operator++() {
    // TODO: Optimize?
    auto node = nodes_.end();
    for (--node; ++node->second == node->first.end(); --node) {
      if (node == nodes_.begin())
        return *this;  // end
    }
    uint32_t node_block_number = (*node->second).second;  // todo: by ref
    for (++node; node != nodes_.end(); ++node) {
      node->first = {throw_if_error(area_->GetMetadataBlock(node_block_number))};
      node->second = node->first.begin();
      node_block_number = (*node->second).second;  // todo: by ref
    }
    return *this;
  }

  EPTreeIterator& operator--() {
    auto node = nodes_.end();
    for (--node; ++node->second == node->first.begin(); --node) {
      if (node == nodes_.begin())
        return *this;  // begin
    }
    --node->second;
    uint32_t node_block_number = (*node->second).second;  // todo: by ref
    for (++node; node != nodes_.end(); ++node) {
      node->first = {throw_if_error(area_->GetMetadataBlock(node_block_number))};
      node->second = node->first.end();
      --node->second;
      node_block_number = (*node->second).second;  // todo: by ref
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
    return nodes_.back().second == other.nodes_.back().second;
  }
  bool operator!=(const EPTreeIterator& other) const { return !operator==(other); }

 private:
  std::vector<node_iterator_info> nodes_;
  std::shared_ptr<Area> area_;
};

class FreeBlocksAllocator : EPTreeBlock {
 public:
  using iterator = EPTreeIterator;
  using const_iterator = const iterator;

  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block)
      : EPTreeBlock(std::move(block)), area_(std::move(area)) {}

  // size_t size() { return header_->block_number; }
  iterator begin() const {
    if (tree_header()->current_tree.items_count.value() == 0)
      return iterator({}, area_);
    std::vector<typename iterator::node_iterator_info> nodes;
    uint32_t node_block_number = 0;
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      RTree node_tree(i == 0 ? original_block() : throw_if_error(area_->GetMetadataBlock(node_block_number)));
      nodes.push_back({node_tree, node_tree.begin()});
      node_block_number = (*nodes.back().second).second;  // todo: by ref
    }
    return iterator(std::move(nodes), area_);
  }
  iterator end() const {
    if (tree_header()->current_tree.items_count.value() == 0)
      return iterator({}, area_);
    std::vector<typename iterator::node_iterator_info> nodes;
    uint32_t node_block_number = 0;
    for (int i = 0; i < tree_header()->depth.value(); i++) {
      RTree node(i == 0 ? original_block() : throw_if_error(area_->GetMetadataBlock(node_block_number)));
      nodes.push_back({node, node.end()});
      auto prev = node.end();
      node_block_number = (*--prev).second;  // todo: by ref
    }
    return iterator(std::move(nodes), area_);
  }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

 private:
  std::shared_ptr<Area> area_;
};
