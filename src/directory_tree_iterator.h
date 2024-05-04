/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <deque>
#include <ranges>

#include "directory_tree_node.h"

template <typename LeafValueType>
struct DiretoryTreeItem {
  std::string key;
  LeafValueType value;
};

template <typename LeafValueType>
class DirectoryTreeIterator {
 public:
  struct item_ref {
    const std::string key;
    dir_tree_node_value_ref<LeafValueType> value;

    operator DiretoryTreeItem<LeafValueType>() const { return {key, value}; }
  };

  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = ptrdiff_t;

  using value_type = DiretoryTreeItem<LeafValueType>;
  using ref_type = item_ref;

  using reference = ref_type;

  using node_info = dir_node_iterator_info<DirectoryTreeNode<LeafValueType>>;

  DirectoryTreeIterator() = default;
  DirectoryTreeIterator(Block* block, std::deque<node_info> nodes) : block_(block), nodes_(std::move(nodes)) {}

  reference operator*() const { return {key(), (*nodes_.back().iterator).value}; }

  DirectoryTreeIterator& operator++() {
    assert(!is_end());
    if (std::ranges::all_of(nodes_, [](const auto& node) { return (node.iterator + 1).is_end(); })) {
      // end
      ++nodes_.back().iterator;
      return *this;
    }

    while ((++nodes_.back().iterator).is_end()) {
      nodes_.pop_back();
    }

    do {
      uint16_t node_offset = (*nodes_.back().iterator).value;
      nodes_.push_back({dir_tree_node_ref::create<LeafValueType>(block_, node_offset)});
      nodes_.back().iterator = nodes_.back().node->begin();
    } while (!nodes_.back().iterator.is_leaf_value());

    return *this;
  }

  DirectoryTreeIterator& operator--() {
    assert(!is_begin());
    while (nodes_.back().iterator.is_begin()) {
      nodes_.pop_back();
    }

    --nodes_.back().iterator;
    while (!nodes_.back().iterator.is_leaf_value()) {
      uint16_t node_offset = (*nodes_.back().iterator).value;
      nodes_.push_back({dir_tree_node_ref::create<LeafValueType>(block_, node_offset)});
      nodes_.back().iterator = nodes_.back().node->end();
      --nodes_.back().iterator;
    }

    return *this;
  }

  DirectoryTreeIterator operator++(int) {
    DirectoryTreeIterator tmp(*this);
    ++(*this);
    return tmp;
  }

  DirectoryTreeIterator operator--(int) {
    DirectoryTreeIterator tmp(*this);
    --(*this);
    return tmp;
  }

  bool operator==(const DirectoryTreeIterator& other) const {
    if (nodes_.empty() || other.nodes_.empty())
      return nodes_.empty() && other.nodes_.empty();  // to do need to check that belongs to same PTRee
    return nodes_.back().iterator == other.nodes_.back().iterator;
  }

  std::deque<node_info>& nodes() { return nodes_; };
  const std::deque<node_info>& nodes() const { return nodes_; };

  bool is_begin() const {
    return std::ranges::all_of(nodes_, [](const auto& node) { return node.iterator.is_begin(); });
  }
  bool is_end() const { return nodes_.empty() || nodes_.back().iterator.is_end(); }

 private:
  std::string key() const {
    return nodes_ | std::views::transform([](const auto& node) {
             char key = (*node.iterator).key;
             auto prefix = node.node->prefix() | std::ranges::to<std::string>();
             return key ? prefix + key : prefix;
           }) |
           std::views::join | std::ranges::to<std::string>();
  };

  Block* block_;
  std::deque<node_info> nodes_;
};
