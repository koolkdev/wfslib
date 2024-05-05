/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <vector>

#include "ptree_node.h"
#include "tree_utils.h"

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
  PTreeIterator(Block* block, std::vector<parent_node_info> parents, std::optional<leaf_node_info> leaf)
      : block_(block), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

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
      uint16_t node_offset = (*rparent->iterator).value();
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{{block_, node_offset}}};
        new_parent.iterator = new_parent.node.begin();
        *parent = std::move(new_parent);
        node_offset = (*parent->iterator).value();
      }
      leaf_node_info new_leaf{{{block_, node_offset}}};
      new_leaf.iterator = new_leaf.node.begin();
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
      uint16_t node_offset = (*--rparent->iterator).value();
      for (auto parent = rparent.base(); parent != parents_.end(); ++parent) {
        parent_node_info new_parent{{{block_, node_offset}}};
        new_parent.iterator = new_parent.node.end();
        --new_parent.iterator;
        *parent = std::move(new_parent);
        node_offset = (*parent->iterator).value();
      }
      leaf_node_info new_leaf{{{block_, node_offset}}};
      new_leaf.iterator = new_leaf.node.end();
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
  Block* block_{nullptr};
  std::vector<parent_node_info> parents_;
  std::optional<leaf_node_info> leaf_;
};
