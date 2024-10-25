/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
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
  using parent_node_info = dir_node_iterator_info<DirectoryTreeNode<LeafValueType>>;
  using leaf_node_info = dir_tree_leaf_node_item_ref<LeafValueType>;

  struct item_ref {
    item_ref() = default;
    item_ref(std::string key, leaf_node_info leaf) : key_(std::move(key)), leaf_(leaf) {}

    const std::string& key() const { return key_; }
    LeafValueType value() const { return leaf_.value(); }
    void set_value(LeafValueType value) { leaf_.set_value(value); }

    operator DiretoryTreeItem<LeafValueType>() const { return {key(), value()}; }

   private:
    const std::string key_;
    leaf_node_info leaf_;
  };

  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = ptrdiff_t;

  using value_type = DiretoryTreeItem<LeafValueType>;
  using ref_type = item_ref;

  using reference = ref_type;

  DirectoryTreeIterator() = default;
  DirectoryTreeIterator(Block* block, std::vector<parent_node_info> parents, std::optional<leaf_node_info> leaf)
      : block_(block), parents_(std::move(parents)), leaf_(std::move(leaf)) {}
  DirectoryTreeIterator(const DirectoryTreeIterator& other) = default;

  DirectoryTreeIterator& operator=(const DirectoryTreeIterator& other) {
    block_ = other.block_;
    parents_ = other.parents_;
    // Regular assigment to assigned leaf_ will do assigment to value of leaf_ instead of copying it.
    leaf_.reset();
    leaf_ = other.leaf_;
    return *this;
  }

  reference operator*() const { return {key(), *leaf_}; }

  DirectoryTreeIterator& operator++() {
    assert(!is_end());

    parents_.push_back({leaf_->get_node()});
    parents_.back().iterator = parents_.back().node.begin();
    leaf_.reset();

    if (parents_.back().iterator.is_end()) {
      std::vector<parent_node_info> removed_parents;
      do {
        removed_parents.push_back(std::move(parents_.back()));
        parents_.pop_back();
      } while (!parents_.empty() && (++parents_.back().iterator).is_end());

      if (parents_.empty()) {
        // end
        for (auto& parent : std::views::reverse(removed_parents)) {
          if (parent.iterator != parent.node.begin())
            --parent.iterator;
          parents_.push_back(std::move(parent));
        }
        return *this;
      }
    }

    while (true) {
      uint16_t node_offset = (*parents_.back().iterator).value();
      decltype(parent_node_info::node) parent{dir_tree_node_ref<LeafValueType>::load(block_, node_offset)};
      if (parent.has_leaf()) {
        leaf_ = parent.leaf_ref();
        return *this;
      }
      parents_.push_back({parent, parent.begin()});
    }
  }

  DirectoryTreeIterator& operator--() {
    assert(!is_begin());

    leaf_.reset();

    while (parents_.back().iterator.is_begin()) {
      auto parent = parents_.back();
      parents_.pop_back();
      if (parent.node.has_leaf()) {
        leaf_ = parent.node.leaf_ref();
        return *this;
      }
    }

    while (true) {
      --parents_.back().iterator;
      uint16_t node_offset = (*parents_.back().iterator).value();
      decltype(parent_node_info::node) parent{dir_tree_node_ref<LeafValueType>::load(block_, node_offset)};
      if (parent.size() == 0) {
        assert(parent.has_leaf());
        leaf_ = parent.leaf_ref();
        return *this;
      }
      parents_.push_back({parent, parent.end()});
    }
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
    if (!leaf_.has_value() || !other.leaf_.has_value())
      return !leaf_.has_value() && !other.leaf_.has_value();  // to do need to check that belongs to same tree
    return leaf_->get_node() == other.leaf_->get_node();
  }

  std::vector<parent_node_info>& parents() { return parents_; };
  const std::vector<parent_node_info>& parents() const { return parents_; };
  leaf_node_info& leaf() { return *leaf_; }
  const leaf_node_info& leaf() const { return *leaf_; }

  bool is_begin() const {
    return parents_.empty() ||
           (leaf_ && std::ranges::all_of(parents_, [](const auto& parent) { return parent.iterator.is_begin(); }));
  }
  bool is_end() const { return !leaf_.has_value(); }

 private:
  std::string key() const {
    return (parents_ | std::views::transform([](const auto& parent) {
              return (parent.node.prefix() | std::ranges::to<std::string>()) + (*parent.iterator).key();
            }) |
            std::views::join | std::ranges::to<std::string>()) +
           (leaf_->get_node().prefix() | std::ranges::to<std::string>());
  };

  Block* block_;
  std::vector<parent_node_info> parents_;
  std::optional<leaf_node_info> leaf_;
};
