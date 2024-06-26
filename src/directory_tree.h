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

  DirectoryTree() = default;
  DirectoryTree(std::shared_ptr<Block> block) : SubBlockAllocator<DirectoryTreeHeader>(std::move(block)) {}
  virtual ~DirectoryTree() = default;

  size_t size() const { return extra_header()->records_count.value(); }
  bool empty() const { return size() == 0; }

  iterator begin() const {
    if (size() == 0)
      return {block().get(), {}, {}};
    std::deque<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    while (true) {
      decltype(iterator::parent_node_info::node) parent{
          dir_tree_node_ref<LeafValueType>::create(block().get(), node_offset)};
      if (parent.has_leaf()) {
        return {block().get(), std::move(parents), parent.leaf()};
      }
      parents.push_back({parent, parent.begin()});
      node_offset = (*parents.back().iterator).value();
    }
  }

  iterator end() const {
    if (size() == 0)
      return {block().get(), {}, {}};
    std::deque<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    while (true) {
      parents.push_back({dir_tree_node_ref<LeafValueType>::create(block().get(), node_offset)});
      parents.back().iterator = parents.back().node.end();
      if (parents.back().node.size() == 0) {
        return {block().get(), std::move(parents), {}};
      }
      --parents.back().iterator;
      node_offset = (*parents.back().iterator).value();
    }
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
    std::deque<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key.begin();
    while (true) {
      decltype(iterator::parent_node_info::node) parent{
          dir_tree_node_ref<LeafValueType>::create(block().get(), node_offset)};
      auto prefix = parent.prefix();
      auto [key_it, prefix_it] = std::ranges::mismatch(std::ranges::subrange(current_key, key.end()), prefix);
      if (key_it == key.end()) {
        // Got to end of our key
        if (prefix_it == prefix.end()) {
          // Full match, check if there is a node
          if (parent.has_leaf()) {
            // Found exact match!
            return {block().get(), std::move(parents), parent.leaf()};
          }
        }
      } else if (prefix_it == prefix.end()) {
        // We matched the prefix
        auto iterator = parent.find(*key_it, exact_match);
        if (iterator != parent.end() && (*iterator).key() >= *key_it) {
          current_key = key_it + 1;
          node_offset = (*iterator).value();
          parents.push_back({parent, iterator});
          continue;
        }
      }
      if (exact_match)
        return end();
      if (prefix_it == prefix.end() || std::lexicographical_compare(key_it, key.end(), prefix_it, prefix.end())) {
        // We are smaller than current prefix + smallest key, which mean that we overshoot, go back to last leaf by
        // getting next leaf first.
        while (!parent.has_leaf()) {
          parents.push_back({parent, parent.begin()});
          node_offset = (*parents.back().iterator).value();
          parent = dir_tree_node_ref<LeafValueType>::create(block().get(), node_offset);
        }
        iterator res{block().get(), std::move(parents), parent.leaf()};
        if (!res.is_begin())
          --res;
        return res;
      } else {
        // Since we are bigger lexicographical, we need to go to the last node
        while (parent.size()) {
          parents.push_back({parent, --parent.end()});
          node_offset = (*parents.back().iterator).value();
          parent = dir_tree_node_ref<LeafValueType>::create(block().get(), node_offset);
        }
        assert(parent.has_leaf());
        return {block().get(), std::move(parents), parent.leaf()};
      }
    }
  }

 private:
  std::pair<iterator, std::string_view::const_iterator> find_for_insert(std::string_view key) const {
    if (size() == 0)
      return {end(), {}};
    std::deque<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key.begin();
    while (true) {
      decltype(iterator::parent_node_info::node) parent{
          dir_tree_node_ref<LeafValueType>::create(block().get(), node_offset)};
      auto prefix = parent.prefix();
      auto [key_it, prefix_it] = std::ranges::mismatch(std::ranges::subrange(current_key, key.end()), prefix);
      if (key_it == key.end()) {
        // Got to end of our key
        if (prefix_it == prefix.end()) {
          // Full match, check if there is a node
          if (parent.has_leaf()) {
            // Found exact match!
            return {{block().get(), std::move(parents), parent.leaf()}, prefix_it};
          }
        }
      } else if (prefix_it == prefix.end()) {
        // Got to end of prefix, but there is still more key
        auto iterator = parent.find(*key_it, /*exact_match=*/false);
        if (iterator != parent.end() && (*iterator).key() >= *key_it) {
          current_key = key_it + 1;
          node_offset = (*iterator).value();
          parents.push_back({parent, iterator});
          continue;
        }
      }
      // Mismatch
      return {{block(), std::move(parents), {}}, prefix_it};
    }
  }
};
