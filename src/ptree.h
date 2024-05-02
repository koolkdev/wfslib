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

#include "ptree_node.h"
#include "tree_nodes_allocator.h"
#include "tree_utils.h"

class Block;

template <typename T>
PTreeNode<T>::iterator split_point(const PTreeNode<T>& node,
                                   const typename PTreeNode<T>::iterator& pos,
                                   key_type& split_key);

template <>
PTreeNode<PTreeNode_details>::iterator split_point(const PTreeNode<PTreeNode_details>& node,
                                                   const typename PTreeNode<PTreeNode_details>::iterator& pos,
                                                   key_type& split_key);

class NodeRefCreator {
 public:
  virtual node_ref get_node_ref(uint16_t offset) const = 0;
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails>
class PTreeIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;
  using value_type = typename PTreeNodeIterator<LeafNodeDetails>::value_type;
  using ref_type = typename PTreeNodeIterator<LeafNodeDetails>::ref_type;

  using reference = typename PTreeNodeIterator<LeafNodeDetails>::reference;

  using parent_node_info = node_iterator_info<PTreeNode<ParentNodeDetails>>;
  using leaf_node_info = node_iterator_info<PTreeNode<LeafNodeDetails>>;

  PTreeIterator() = default;
  PTreeIterator(const NodeRefCreator* ptree, std::vector<parent_node_info> parents, std::optional<leaf_node_info> leaf)
      : ptree_(ptree), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

  reference operator*() const { return *leaf_->iterator; }

  PTreeIterator& operator++() {
    assert(!is_end());
    if ((++leaf_->iterator).is_end()) {
      if (parents_.empty())
        return *this;  // end
      auto rparent = parents_.rbegin();
      while ((++rparent->iterator).is_end()) {
        if (++rparent == parents_.rend()) {
          while (--rparent != parents_.rbegin())
            --rparent->iterator;
          --rparent->iterator;
          return *this;  // end
        }
      }
      uint16_t node_offset = (*rparent->iterator).value;
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{ptree_->get_node_ref(node_offset)}};
        new_parent.iterator = new_parent.node->begin();
        *parent = std::move(new_parent);
        node_offset = (*parent->iterator).value;
      }
      leaf_node_info new_leaf{{ptree_->get_node_ref(node_offset)}};
      new_leaf.iterator = new_leaf.node->begin();
      leaf_ = std::move(new_leaf);
    }
    return *this;
  }

  PTreeIterator& operator--() {
    assert(!is_begin());
    if (leaf_->iterator.is_begin()) {
      if (parents_.empty())
        return *this;  // begin
      auto rparent = parents_.rbegin();
      while (rparent->iterator.is_begin()) {
        if (++rparent == parents_.rend())
          return *this;  // begin
      }
      uint16_t node_offset = (*--rparent->iterator).value;
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{ptree_->get_node_ref(node_offset)}};
        new_parent.iterator = new_parent.node->end();
        --new_parent.iterator;
        *parent = std::move(new_parent);
        node_offset = (*parent->iterator).value;
      }
      leaf_node_info new_leaf{{ptree_->get_node_ref(node_offset)}};
      new_leaf.iterator = new_leaf.node->end();
      leaf_ = std::move(new_leaf);
    }
    --leaf_->iterator;
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

  bool operator==(const PTreeIterator& other) const {
    if (!leaf_ || !other.leaf_)
      return !leaf_ && !other.leaf_;  // to do need to check that belongs to same PTRee
    return leaf_->iterator == other.leaf_->iterator;
  }

  leaf_node_info& leaf() { return *leaf_; }
  const leaf_node_info& leaf() const { return *leaf_; }
  std::vector<parent_node_info>& parents() { return parents_; };
  const std::vector<parent_node_info>& parents() const { return parents_; };

  bool is_begin() const {
    return !leaf_ || (std::ranges::all_of(parents_, [](const auto& parent) { return parent.iterator.is_begin(); }) &&
                      leaf_->iterator.is_begin());
  }
  bool is_end() const { return !leaf_ || leaf_->iterator.is_end(); }

 private:
  const NodeRefCreator* ptree_{nullptr};
  std::vector<parent_node_info> parents_;
  std::optional<leaf_node_info> leaf_;
};

template <is_parent_node_details ParentNodeDetails, is_leaf_node_details LeafNodeDetails, typename AllocatorArgs>
class PTree : public TreeNodesAllocator<AllocatorArgs>, public NodeRefCreator {
 public:
  using iterator = PTreeIterator<ParentNodeDetails, LeafNodeDetails>;
  static_assert(std::bidirectional_iterator<iterator>);

  using parent_node = PTreeNode<ParentNodeDetails>;
  using leaf_node = PTreeNode<LeafNodeDetails>;

  static_assert(std::ranges::bidirectional_range<parent_node>);
  static_assert(std::ranges::bidirectional_range<leaf_node>);

  PTree(std::shared_ptr<Block> block) : TreeNodesAllocator<AllocatorArgs>(std::move(block)) {}
  virtual ~PTree() = default;

  virtual uint16_t extra_info() const { return 0; }
  node_ref get_node_ref(uint16_t offset) const override { return {this->block().get(), offset, extra_info()}; }

  virtual PTreeHeader* mutable_header() = 0;
  virtual const PTreeHeader* header() const = 0;

  size_t size() const { return header()->items_count.value(); }
  bool empty() const { return size() == 0; }

  iterator begin() const {
    if (size() == 0)
      return {this, {}, std::nullopt};
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename iterator::parent_node_info parent{{get_node_ref(node_offset)}};
      parent.iterator = parent.node->begin();
      parents.push_back(std::move(parent));
      node_offset = (*parents.back().iterator).value;
    }
    typename iterator::leaf_node_info leaf{{get_node_ref(node_offset)}};
    leaf.iterator = leaf.node->begin();
    return {this, std::move(parents), std::move(leaf)};
  }

  iterator end() const {
    if (size() == 0)
      return {this, {}, std::nullopt};
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename iterator::parent_node_info parent{{get_node_ref(node_offset)}};
      parent.iterator = parent.node->end();
      --parent.iterator;
      parents.push_back(std::move(parent));
      node_offset = (*parents.back().iterator).value;
    }
    typename iterator::leaf_node_info leaf{{get_node_ref(node_offset)}};
    leaf.iterator = leaf.node->end();
    return {this, std::move(parents), std::move(leaf)};
  }

  iterator middle() const {
    auto it = begin();
    for ([[maybe_unused]] auto _ : std::views::iota(size_t{0}, size() / 2)) {
      ++it;
    }
    return it;
  }
  iterator find(key_type key, bool exact_match = true) const {
    if (size() == 0)
      return {this, {}, std::nullopt};  // TODO empty tree iterator constructor
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = header()->root_offset.value();
    for (int i = 0; i < header()->tree_depth.value(); ++i) {
      typename iterator::parent_node_info parent{{get_node_ref(node_offset)}};
      parent.iterator = parent.node->find(key, false);
      assert(!parent.iterator.is_end());
      parents.push_back(std::move(parent));
      node_offset = (*parents.back().iterator).value;
    }
    typename iterator::leaf_node_info leaf{{get_node_ref(node_offset)}};
    leaf.iterator = leaf.node->find(key, exact_match);
    return {this, std::move(parents), std::move(leaf)};
  }

  LeafNodeDetails* grow_tree(const iterator& pos, key_type split_key) {
    uint16_t nodes_to_alloc = 1;
    auto parent = pos.parents().rbegin();
    while (parent != pos.parents().rend() && parent->node->full()) {
      ++nodes_to_alloc;
      ++parent;
    }
    if (parent == pos.parents().rend()) {
      if (pos.parents().size() == 4) {
        // can't grow anymore in depth
        return nullptr;
      }
      // New root
      ++nodes_to_alloc;
    }
    auto* nodes = this->template Alloc<ParentNodeDetails>(nodes_to_alloc);
    if (!nodes)
      return nullptr;
    auto* new_child_node = &nodes[0];
    auto* node = &nodes[1];
    auto child_node_offset = this->to_offset(new_child_node);
    auto child_split_key = split_key;
    for (parent = pos.parents().rbegin(); parent != pos.parents().rend() && parent->node->full(); ++parent) {
      auto parent_split_pos = split_point(*parent->node, parent->iterator, split_key);
      uint16_t node_offset = this->to_offset(node++);
      parent_node new_node{get_node_ref(node_offset), 0};
      new_node.clear(true);
      if (child_split_key == split_key) {
        // We need to insert it first or we will loose the the key. (since we don't store the key for [0])
        new_node.insert(new_node.begin(), {child_split_key, child_node_offset});
      }
      new_node.insert(new_node.end(), parent_split_pos, parent->node->end());
      parent->node->erase(parent_split_pos, parent->node->end());
      if (child_split_key > split_key) {
        new_node.insert(new_node.begin() + ((parent->iterator - parent_split_pos) + 1),
                        {child_split_key, child_node_offset});
      } else if (child_split_key < split_key) {
        parent->node->insert(parent->iterator + 1, {child_split_key, child_node_offset});
      }
      child_node_offset = node_offset;
      child_split_key = split_key;
    }
    if (parent == pos.parents().rend()) {
      // new root
      uint16_t node_offset = this->to_offset(node++);
      parent_node new_node{get_node_ref(node_offset), 0};
      new_node.clear(true);
      new_node.insert(new_node.end(), {0, header()->root_offset.value()});
      new_node.insert(new_node.end(), {child_split_key, child_node_offset});
      mutable_header()->root_offset = node_offset;
      mutable_header()->tree_depth = header()->tree_depth.value() + 1;
    } else {
      parent->node->insert(parent->iterator + 1, {child_split_key, child_node_offset});
    }
    assert(node - new_child_node == nodes_to_alloc);
    return reinterpret_cast<LeafNodeDetails*>(new_child_node);
  }

  bool insert(const typename iterator::value_type& key_val) {
    auto pos = find(key_val.key, false);
    if (!pos.is_end() && (*pos).key == key_val.key) {
      // key already exists
      return false;
    }
    return insert(pos, key_val);
  }

  bool insert(const iterator& pos, const typename iterator::value_type& key_val) {
    auto items_count = header()->items_count.value();
    if (items_count == 0) {
      // first item in tree
      auto* node = this->template Alloc<LeafNodeDetails>(1);
      if (!node) {
        // Tree is full
        return false;
      }
      uint16_t node_offset = this->to_offset(node);
      leaf_node new_node{get_node_ref(node_offset), 0};
      new_node.clear(true);
      new_node.insert(new_node.begin(), key_val);
      auto* header = mutable_header();
      header->items_count = 1;
      header->root_offset = node_offset;
      header->tree_depth = 0;
      return true;
    }
    auto leaf_pos_to_insert = key_val.key < (*pos).key ? pos.leaf().iterator : pos.leaf().iterator + 1;
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
    leaf_node new_node{get_node_ref(this->to_offset(node)), 0};
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
    std::vector<typename PTreeNodeIterator<ParentNodeDetails>::value_type> current_nodes;
    for (auto it = it_start; it != it_end;) {
      auto* node = this->template Alloc<LeafNodeDetails>(1);
      leaf_node new_node{get_node_ref(this->to_offset(node)), 0};
      new_node.clear(true);
      auto range_start = it;
      uint16_t added_items = 0;
      for (; added_items < 5 && it != it_end; ++added_items, ++it) {
      }
      new_node.insert(new_node.begin(), range_start, it);
      header->items_count += static_cast<uint16_t>(new_node.size());
      current_nodes.push_back({(*new_node.begin()).key, this->to_offset(node)});
    }

    // Create the tree
    while (current_nodes.size() > 1) {
      header->tree_depth += 1;
      std::vector<typename PTreeNodeIterator<ParentNodeDetails>::value_type> new_nodes;
      for (auto it = current_nodes.begin(); it != current_nodes.end();) {
        auto* node = this->template Alloc<ParentNodeDetails>(1);
        parent_node new_node{get_node_ref(this->to_offset(node)), 0};
        new_node.clear(true);
        auto range_start = it;
        it += std::min<size_t>(5, current_nodes.end() - it);
        new_node.insert(new_node.begin(), range_start, it);
        new_nodes.push_back({range_start->key, this->to_offset(node)});
      }
      current_nodes.swap(new_nodes);
    }

    if (current_nodes.size()) {
      // Update the root
      header->root_offset = current_nodes[0].value;
    }

    return true;
  }

  void erase(const iterator& pos) {
    pos.leaf().node->erase(pos.leaf().iterator);
    if (pos.leaf().node->empty()) {
      this->Free(pos.leaf().node->node(), 1);
      auto parent = pos.parents().rbegin();
      for (; parent != pos.parents().rend(); parent++) {
        parent->node->erase(parent->iterator);
        if (!parent->node->empty())
          break;
        this->Free(parent->node->node(), 1);
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

  void erase(const iterator& it_start, const iterator& it_end) {
    // Iterating from last to first is safe as the iterator will always be valid after operator--. It isn't true in the
    // other direction.
    auto it = it_end;
    while (!empty() && it != it_start) {
      erase(--it);
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
