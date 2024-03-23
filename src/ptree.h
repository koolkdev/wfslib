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
#include <type_traits>
#include <vector>

#include "tree_node.h"
#include "tree_utils.h"

class MetadataBlock;

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

template <typename T>
PTreeNode<T>::const_iterator split_point(const PTreeNode<T>& node,
                                         const typename PTreeNode<T>::const_iterator& pos,
                                         key_type& split_key);

template <>
PTreeNode<PTreeNode_details>::const_iterator split_point(
    const PTreeNode<PTreeNode_details>& node,
    const typename PTreeNode<PTreeNode_details>::const_iterator& pos,
    key_type& split_key);

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails>
class PTreeConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;
  using value_type = typename PTreeNodeIterator<LeafNodeDetails>::value_type;
  using ref_type = typename PTreeNodeIterator<LeafNodeDetails>::ref_type;

  using const_reference = typename PTreeNodeIterator<LeafNodeDetails>::const_reference;
  using const_pointer = typename PTreeNodeIterator<LeafNodeDetails>::const_pointer;

  using reference = const_reference;
  using pointer = const_pointer;

  using parent_node_info = node_iterator_info<PTreeNode<ParentNodeDetails>>;
  using leaf_node_info = node_iterator_info<PTreeNode<LeafNodeDetails>>;

  PTreeConstIterator() = default;
  PTreeConstIterator(std::shared_ptr<Block> block,
                     std::vector<parent_node_info> parents,
                     std::optional<leaf_node_info> leaf)
      : block_(block), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

  reference operator*() const { return *leaf_->iterator; }
  pointer operator->() const { return leaf_->iterator.operator->(); }

  PTreeConstIterator& operator++() {
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
      uint16_t node_offset = rparent->iterator->value;
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{{block_, node_offset}}};
        new_parent.iterator = new_parent.node->begin();
        *parent = std::move(new_parent);
        node_offset = parent->iterator->value;
      }
      leaf_node_info new_leaf{{{block_, node_offset}}};
      new_leaf.iterator = new_leaf.node->begin();
      leaf_ = std::move(new_leaf);
    }
    return *this;
  }

  PTreeConstIterator& operator--() {
    assert(!is_begin());
    if (leaf_->iterator.is_begin()) {
      if (parents_.empty())
        return *this;  // begin
      auto rparent = parents_.rbegin();
      while (rparent->iterator.is_begin()) {
        if (++rparent == parents_.rend())
          return *this;  // begin
      }
      uint16_t node_offset = (--rparent->iterator)->value;
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{{block_, node_offset}}};
        new_parent.iterator = new_parent.node->end();
        --new_parent.iterator;
        *parent = std::move(new_parent);
        node_offset = parent->iterator->value;
      }
      leaf_node_info new_leaf{{{block_, node_offset}}};
      new_leaf.iterator = new_leaf.node->end();
      leaf_ = std::move(new_leaf);
    }
    --leaf_->iterator;
    return *this;
  }

  PTreeConstIterator operator++(int) {
    PTreeConstIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  PTreeConstIterator operator--(int) {
    PTreeConstIterator tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const PTreeConstIterator& other) const {
    if (!leaf_ || !other.leaf_)
      return !leaf_ && !other.leaf_;  // to do need to check that belongs to same PTRee
    return leaf_->iterator == other.leaf_->iterator;
  }

  leaf_node_info& leaf() { return *leaf_; }
  const leaf_node_info& leaf() const { return *leaf_; }
  std::vector<parent_node_info>& parents() { return parents_; };
  const std::vector<parent_node_info>& parents() const { return parents_; };

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

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails>
class PTreeIterator : public PTreeConstIterator<ParentNodeDetails, LeafNodeDetails> {
 public:
  using base = PTreeConstIterator<ParentNodeDetails, LeafNodeDetails>;

  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;
  using value_type = base::value_type;
  using ref_type = base::ref_type;

  using reference = ref_type;
  using pointer = ref_type*;

  using parent_node_info = base::parent_node_info;
  using leaf_node_info = base::leaf_node_info;

  PTreeIterator() = default;
  PTreeIterator(std::shared_ptr<Block> block, std::vector<parent_node_info> parents, std::optional<leaf_node_info> leaf)
      : base(std::move(block), std::move(parents), std::move(leaf)) {}

  reference operator*() const { return *base::leaf().iterator; }
  pointer operator->() const { return base::leaf().iterator.operator->(); }

  PTreeIterator& operator++() {
    base::operator++();
    return *this;
  }

  PTreeIterator& operator--() {
    base::operator--();
    return *this;
  }

  PTreeIterator operator++(int) {
    PTreeIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  PTreeIterator operator--(int) {
    PTreeIterator tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const PTreeIterator& other) const { return base::operator==(other); }
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename Allocator>
  requires nodes_allocator<Allocator, ParentNodeDetails> && nodes_allocator<Allocator, LeafNodeDetails> &&
           std::bidirectional_iterator<PTreeConstIterator<ParentNodeDetails, LeafNodeDetails>> &&
           std::bidirectional_iterator<PTreeIterator<ParentNodeDetails, LeafNodeDetails>>
class PTree : public Allocator {
 public:
  using iterator = PTreeIterator<ParentNodeDetails, LeafNodeDetails>;
  using const_iterator = PTreeConstIterator<ParentNodeDetails, LeafNodeDetails>;
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  using parent_node = PTreeNode<ParentNodeDetails>;
  using leaf_node = PTreeNode<LeafNodeDetails>;

  PTree(std::shared_ptr<MetadataBlock> block) : Allocator(std::move(block)) {}
  virtual ~PTree() = default;

  virtual PTreeHeader* mutable_header() = 0;
  virtual const PTreeHeader* header() const = 0;

  size_t size() const { return header()->items_count.value(); }
  bool empty() const { return size() == 0; }

  iterator begin() { return begin_impl(); }
  iterator end() { return end_impl(); }
  const_iterator begin() const { return begin_impl(); }
  const_iterator end() const { return end_impl(); }

  reverse_iterator rbegin() { return reverse_iterator{end()}; }
  reverse_iterator rend() { return reverse_iterator{begin()}; }
  const_reverse_iterator rbegin() const { return const_reverse_iterator{end()}; }
  const_reverse_iterator rend() const { return const_reverse_iterator{begin()}; }

  const_iterator middle() const {
    auto it = begin();
    for ([[maybe_unused]] auto _ : std::views::iota(0, header()->items_count.value() / 2)) {
      ++it;
    }
    return it;
  }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  iterator find(key_type key, bool exact_match = true) { return find_impl(key, exact_match); }
  const_iterator find(key_type key, bool exact_match = true) const { return find_impl(key, exact_match); }

  LeafNodeDetails* grow_tree(const const_iterator& pos, key_type split_key) {
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
      auto parent_split_pos = split_point(*parent->node, parent->iterator, split_key);
      parent_node new_node{{this->block(), this->to_offset(node)}, 0};
      new_node.clear(true);
      new_node.insert(new_node.begin(), parent_split_pos, parent->node->cend());
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
    auto pos = find(key_val.key, false);
    if (!pos.leaf().iterator.is_end() && pos->key == key_val.key) {
      // key already exists
      return false;
    }
    return insert(pos, key_val);
  }

  bool insert(const const_iterator& pos, const typename iterator::value_type& key_val) {
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
    auto leaf_pos_to_insert = key_val.key < pos->key ? pos.leaf().iterator : pos.leaf().iterator + 1;
    if (!pos.leaf().node->full()) {
      // We have place to add the new key/val
      pos.leaf().node->insert(leaf_pos_to_insert, key_val);
      mutable_header()->items_count = items_count + 1;
      return true;
    }
    auto split_key = key_val.key;
    auto split_pos = split_point(*pos.leaf().node, leaf_pos_to_insert, split_key);
    auto* node = grow_tree(pos, split_key);
    if (!node)
      return false;
    leaf_node new_node{{this->block(), this->to_offset(node)}, 0};
    new_node.clear(true);
    new_node.insert(new_node.begin(), split_pos, pos.leaf().node->cend());
    pos.leaf().node->erase(split_pos, pos.leaf().node->end());
    if (key_val.key >= split_key) {
      new_node.insert(new_node.begin() + (leaf_pos_to_insert - split_pos), key_val);
    } else {
      pos.leaf().node->insert(leaf_pos_to_insert, key_val);
    }
    mutable_header()->items_count = items_count + 1;
    return true;
  }

  bool insert(const const_iterator& it_start, const const_iterator& it_end) {
    for (const auto& val : std::ranges::subrange(it_start, it_end)) {
      if (!insert(val))
        return false;
    }
    return true;
  }

  bool insert_compact(const const_iterator& it_start, const const_iterator& it_end) {
    if (!empty()) {
      assert(false);
      return false;
    }

    // Fill up to 5 items in each edge
    auto* header = mutable_header();
    header->tree_depth = 0;
    std::vector<typename PTreeNodeIterator<ParentNodeDetails>::value_type> current_nodes;
    for (auto it = it_start; it != it_end;) {
      auto* node = this->template Alloc<LeafNodeDetails>(1);
      leaf_node new_node{{this->block(), this->to_offset(node)}, 0};
      new_node.clear(true);
      auto range_start = it;
      uint16_t added_items = 0;
      for (; added_items < 5 && it != it_end; ++added_items, ++it) {
      }
      new_node.insert(new_node.begin(), range_start, it);
      header->items_count += static_cast<uint16_t>(new_node.size());
      current_nodes.push_back({new_node.begin()->key, this->to_offset(node)});
    }

    // Create the tree
    while (current_nodes.size() > 1) {
      header->tree_depth += 1;
      std::vector<typename PTreeNodeIterator<ParentNodeDetails>::value_type> new_nodes;
      for (auto it = current_nodes.begin(); it != current_nodes.end();) {
        auto* node = this->template Alloc<ParentNodeDetails>(1);
        parent_node new_node{{this->block(), this->to_offset(node)}, 0};
        new_node.clear(true);
        auto range_start = it;
        it += std::min<size_t>(5, current_nodes.end() - it);
        new_node.insert(new_node.begin(), range_start, it);
        new_nodes.push_back({new_node.begin()->key, this->to_offset(node)});
      }
      current_nodes.swap(new_nodes);
    }

    if (current_nodes.size()) {
      // Update the root
      header->root_offset = current_nodes[0].value;
    }

    return true;
  }

  void erase(const const_iterator& pos) {
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

  void erase(const const_iterator& it_start, const const_iterator& it_end) {
    for (auto it = it_start; it != it_end; ++it) {
      erase(it);
    }
  }

  void split(PTree& left, PTree& right, const const_iterator& pos) const {
    left.insert(begin(), pos);
    right.insert(pos, end());
  }

  void split(PTree& right, const_iterator& pos) {
    auto end_pos = end();
    right.insert(pos, end_pos);
    erase(pos, end_pos);
  }

  void split_compact(PTree& left, PTree& right, const const_iterator& pos) const {
    left.insert_compact(begin(), pos);
    right.insert_compact(pos, end());
  }

 private:
  iterator begin_impl() const {
    if (size() == 0)
      return {this->block(), {}, std::nullopt};
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename iterator::parent_node_info parent{{{this->block(), node_offset}}};
      parent.iterator = parent.node->begin();
      parents.push_back(std::move(parent));
      node_offset = parents.back().iterator->value;
    }
    typename iterator::leaf_node_info leaf{{{this->block(), node_offset}}};
    leaf.iterator = leaf.node->begin();
    return {this->block(), std::move(parents), std::move(leaf)};
  }

  iterator end_impl() const {
    if (size() == 0)
      return {this->block(), {}, std::nullopt};
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename iterator::parent_node_info parent{{{this->block(), node_offset}}};
      parent.iterator = parent.node->end();
      --parent.iterator;
      parents.push_back(std::move(parent));
      node_offset = parents.back().iterator->value;
    }
    typename iterator::leaf_node_info leaf{{{this->block(), node_offset}}};
    leaf.iterator = leaf.node->end();
    return {this->block(), std::move(parents), std::move(leaf)};
  }

  iterator find_impl(key_type key, bool exact_match = true) const {
    if (size() == 0)
      return {this->block(), {}, std::nullopt};  // TODO empty tree iterator constructor
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename iterator::parent_node_info parent{{{this->block(), node_offset}}};
      parent.iterator = parent.node->find(key, false);
      assert(!parent.iterator.is_end());
      parents.push_back(std::move(parent));
      node_offset = parents.back().iterator->value;
    }
    typename iterator::leaf_node_info leaf{{{this->block(), node_offset}}};
    leaf.iterator = leaf.node->find(key, exact_match);
    return {this->block(), std::move(parents), std::move(leaf)};
  }
};
