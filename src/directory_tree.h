/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <ranges>

#include "directory_tree_iterator.h"
#include "sub_block_allocator.h"

template <typename LeafValueType>
class DirectoryTree : public SubBlockAllocator<DirectoryTreeHeader> {
 public:
  using iterator = DirectoryTreeIterator<LeafValueType>;

  static_assert(std::bidirectional_iterator<iterator>);

  using node = DirectoryTreeNode<LeafValueType>;

  DirectoryTree(std::shared_ptr<Block> block) : SubBlockAllocator<DirectoryTreeHeader>(std::move(block)) {}
  virtual ~DirectoryTree() = default;

  size_t size() const { return extra_header()->records_count.value(); }
  bool empty() const { return size() == 0; }

  iterator begin() const {
    if (size() == 0)
      return {block().get(), {}};
    std::deque<typename iterator::node_info> nodes;
    do {
      uint16_t node_offset = nodes.empty() ? extra_header()->root.value() : (*nodes.back().iterator).value;
      nodes.push_back({dir_tree_node_ref::create<LeafValueType>(block().get(), node_offset)});
      nodes.back().iterator = nodes.back().node->begin();
    } while (!nodes.back().iterator.is_leaf_value());
    return {block().get(), std::move(nodes)};
  }
  iterator end() const {
    if (size() == 0)
      return {block().get(), {}};
    std::deque<typename iterator::node_info> nodes;
    do {
      uint16_t node_offset = nodes.empty() ? extra_header()->root.value() : (*nodes.back().iterator).value;
      nodes.push_back({dir_tree_node_ref::create<LeafValueType>(block().get(), node_offset)});
      nodes.back().iterator = nodes.back().node->end();
      assert(!nodes.back().iterator.is_begin());
      --nodes.back().iterator;
    } while (!nodes.back().iterator.is_leaf_value());
    ++nodes.back().iterator;
    return {block().get(), std::move(nodes)};
  }

  iterator middle() const {
    auto it = begin();
    for ([[maybe_unused]] auto _ : std::views::iota(size_t{0}, size() / 2)) {
      ++it;
    }
    return it;
  }

  iterator find(std::string_view key, bool exact_match = true) const {
    if (size() == 0)
      return end();
    std::deque<typename iterator::node_info> nodes;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key.begin();
    while (true) {
      nodes.push_back({dir_tree_node_ref::create<LeafValueType>(block().get(), node_offset)});
      auto& node = nodes.back();
      auto prefix = node.node->prefix();
      auto [key_it, prefix_it] = std::ranges::mismatch(std::ranges::subrange(current_key, key.end()), prefix);
      if (key_it == key.end()) {
        // Got to end of our key
        if (prefix_it == prefix.end()) {
          // Full match, check if there is a node
          node.iterator = node.node->begin();
          if (node.iterator.is_leaf_value()) {
            // Found exact match!
            return {block().get(), std::move(nodes)};
          }
        }
      } else if (prefix_it == prefix.end()) {
        // We matched the prefix
        node.iterator = node.node->find(*key_it, exact_match);
        if (node.iterator != node.node->end() && (*node.iterator).key >= *key_it) {
          current_key = key_it + 1;
          node_offset = (*node.iterator).value;
          continue;
        }
      }
      if (exact_match)
        return end();
      if (prefix_it == prefix.end() || std::lexicographical_compare(key_it, key.end(), prefix_it, prefix.end())) {
        // We are smaller than current prefix + smallest key, which mean that we overshoot, go back to last leaf by
        // getting next leaf first.
        while (node.iterator = node.node->begin(), !node.iterator.is_leaf_value()) {
          node_offset = (*node.iterator).value;
          nodes.push_back({dir_tree_node_ref::create<LeafValueType>(block().get(), node_offset)});
          node = nodes.back();
        }
        iterator res{block().get(), std::move(nodes)};
        if (!res.is_begin())
          --res;
        return res;
      } else {
        // Since we are bigger lexicographical, we need to go to the last node
        while (node.iterator = node.node->end(), !(--node.iterator).is_leaf_value()) {
          node_offset = (*node.iterator).value;
          nodes.push_back({dir_tree_node_ref::create<LeafValueType>(block().get(), node_offset)});
          node = nodes.back();
        }
        return {block().get(), std::move(nodes)};
      }
    }
  }

 private:
  std::pair<iterator, std::string_view::const_iterator> find_for_insert(std::string_view key) const {
    if (size() == 0)
      return {end(), {}};
    std::deque<typename iterator::node_info> nodes;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key.begin();
    while (true) {
      nodes.push_back({dir_tree_node_ref::create<LeafValueType>(block(), node_offset)});
      auto& node = nodes.back();
      auto prefix = node.node->prefix();
      auto [key_it, prefix_it] = std::ranges::mismatch(std::ranges::subrange(current_key, key.end()), prefix);
      if (key_it == key.end()) {
        // Got to end of our key
        node.iterator = node.node->begin();
        if (prefix_it == prefix.end()) {
          // Full match, check if there is a node
          if (node.iterator.is_leaf_value()) {
            // Found exact match!
            return {{block(), std::move(nodes)}, prefix_it};
          }
        }
      } else if (prefix_it == prefix.end()) {
        // Got to end of prefix, but there is still more key
        node.iterator = node.node->find(*key_it, /*exact_match=*/false);
        if ((*node.iterator).key == *key_it) {
          // Match, continue searching
          current_key = key_it + 1;
          node_offset = (*node.iterator).value;
          continue;
        }
      }
      // Mismatch
      return {{block(), std::move(nodes)}, prefix_it};
    }
  }
};
