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

  using parent_node = DirectoryTreeNode<LeafValueType>;

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
      parent_node parent{dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)};
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
      parents.push_back({dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)});
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

  bool insert(const typename iterator::value_type& key_val) {
    // The caller to it:
    // while (!insert(pos, key_val)) {
    //   split() // check
    //   update left right keys
    //}
    std::optional<parent_node> new_node;
    if (size() == 0) {
      new_node = alloc_new_node(key_val.key, {}, key_val.value);
      if (!new_node) {
        assert(false);
        return false;
      }
      mutable_extra_header()->root = new_node->offset();
      mutable_extra_header()->records_count += 1;
      return true;
    }

    // Find the position to insert at
    std::deque<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key_val.key.begin();
    while (true) {
      parent_node parent{dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)};
      auto prefix = parent.prefix();
      auto [key_it, prefix_it] = std::ranges::mismatch(std::ranges::subrange(current_key, key_val.key.end()), prefix);
      if (key_it == key_val.key.end()) {
        // Got to end of our key
        if (prefix_it == prefix.end()) {
          // Full match, check if there is a node
          if (parent.has_leaf()) {
            // Found exact match can't insert
            assert(false);
            return false;
          }
          // Insert value
          if (!parent.set_value(key_val.value)) {
            if (!recreate_node(parents, parent, parent.prefix(), parent, key_val.value)) {
              return false;
            }
          }
          mutable_extra_header()->records_count += 1;
          return true;
        }
      } else if (prefix_it == prefix.end()) {
        // Got to end of prefix, but there is still more key
        auto iterator = parent.find(*key_it, /*exact_match=*/false);
        if (iterator != parent.end() && (*iterator).key() == *key_it) {
          // Found match to the next key char
          current_key = key_it + 1;
          node_offset = (*iterator).value();
          parents.push_back({parent, iterator});
          continue;
        }
        // Insert new key to parent
        new_node = alloc_new_node({key_it + 1, key_val.key.end()}, {}, key_val.value);
        if (!new_node) {
          return false;
        }
        if (!parent.insert(iterator, {*key_it, new_node->offset()})) {
          auto vals = std::ranges::to<std::vector<dir_tree_parent_node_item>>(parent);
          vals.insert(vals.begin() + (iterator - parent.begin()), {*key_it, new_node->offset()});
          if (!recreate_node(parents, parent, prefix, vals, parent.leaf())) {
            Free(new_node.offset());
            return false;
          }
        }
        mutable_extra_header()->records_count += 1;
        return true;
      } else {
        new_node = alloc_new_node({key_it + 1, key_val.key.end()}, key_val.value);
        if (!new_node) {
          return false;
        }
      }
      // Need to split prefix
      bool inserting_value = key_it == key_val.key.end();
      auto new_child = alloc_new_node({prefix_it + 1, prefix.end()}, parent, parent.leaf());
      if (!new_child) {
        if (!inserting_value) {
          Free(new_node->offset(), new_node->allocated_size());
        }
        return false;
      }
      std::string_view new_prefix{prefix.begin(), prefix_it};
      std::deque<typename iterator::value_type> new_nodes{{*prefix_it, new_child->offset()}};
      if (!inserting_value) {
        typename iterator::value_type new_value_pair{*key_it, new_node->offset()};
        if (*prefix_it < *key_it) {
          new_nodes.push_back(new_value_pair);
        } else {
          new_nodes.push_front(new_value_pair);
        }
      }
      if (!recreate_node(parents, parent, {prefix.begin(), prefix_it}, new_nodes, std::nullopt)) {
        assert(false);  // Should always success because of shrinking
        return false;
      }
      mutable_extra_header()->records_count += 1;
      return true;
    }
  }

  void erase(iterator& pos) {
    // Remove currnet parent leaf
    auto parents = pos.parents();
    parent_node current_parent{pos.leaf()->get_node()};
    if (!current_parent.remove_leaf()) {
      if (!recreate_node(parents, current_parent, current_parent.prefix(), current_parent, std::nullopt)) {
        assert(false);  // Always can shrink
        return;
      }
    }
    mutable_extra_header()->records_count -= 1;
    while (true) {
      if (current_parent.has_leaf() || current_parent.size() > 1) {
        return;
      } else if (current_parent.size() == 1) {
        // Can merge with child node
        merge_empty_node(parents, current_parent);
        return;
      }
      // Empty node, remove
      Free(current_parent.offset());
      if (parents.empty()) {
        // Removed last node
        assert(size() == 0);
        return;
      }
      auto parent = parents.back();
      parents.pop_back();
      current_parent = parent.node;
      if (!current_parent.erase(parent.iterator)) {
        auto vals = std::ranges::to<std::vector<dir_tree_parent_node_item>>(current_parent);
        vals.erase(vals.begin() + (parent.iterator - parent.begin()));
        if (!recreate_node(parents, parent, current_parent.prefix(), vals, current_parent.leaf())) {
          assert(false);  // Always can shrink
          return;
        }
      }
    }
  }

  void split(DirectoryTree& left, DirectoryTree& right, const iterator& pos) const {
    // Implemented in a totally different way then original because it is way too complex for no reason.
    std::deque<typename iterator::parent_node_info> parents, new_parents;
    parent_node root_node{dir_tree_node_ref<LeafValueType>::load(block().get(), extra_header()->root.value())};
    split_copy(right, parents, new_parents, root_node, pos.parents(),
               /*left=*/false);
    if (!pos.is_begin()) {
      --pos;
      split_copy(left, parents, new_parents, root_node, pos.parents(),
                 /*left=*/true);
    }
  }

  iterator find(std::string_view key, bool exact_match = true) const {
    if (size() == 0)
      return end();
    std::deque<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key.begin();
    while (true) {
      parent_node parent{dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)};
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
          parent = dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset);
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
          parent = dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset);
        }
        assert(parent.has_leaf());
        return {block().get(), std::move(parents), parent.leaf()};
      }
    }
  }

 protected:
  virtual void copy_value(DirectoryTree& new_tree, parent_node& new_node, LeafValueType value) = 0;

 private:
  template <typename Range>
  void init_new_node(parent_node node,
                     std::string_view prefix,
                     Range& childs,
                     std::optional<LeafValueType> leaf_value) {
    node.clear();
    node.set_prefix(prefix, /*check_size=*/false);
    if (leaf_value.has_value()) {
      node.set_leaf(*leaf_value, /*check_size=*/false);
    }
    node.insert(node.begin(), childs, /*check_size=*/false);
    assert(node.allocated_size() == dir_tree_node_ref<LeafValueType>::get_node_size(node.node()));
  }

  std::optional<parent_node> alloc_new_node(std::string_view prefix,
                                            std::vector<typename iterator::value_type> values,
                                            std::optional<LeafValueType> leaf_value) {
    auto new_size =
        calc_node_size<LeafValueType>(prefix.size(), values.size() + leaf_value.has_value(), leaf_value.has_value());
    auto new_offset = Alloc(new_size);
    if (!new_offset) {
      return std::nullopt;
    }
    parent_node new_node = dir_tree_node_ref<LeafValueType>::create(block().get(), new_offset, new_size);
    init_new_node(new_node, prefix, values, leaf_value);
    return new_node;
  }

  template <typename Range>
  bool recreate_node(std::deque<typename iterator::parent_node_info>& parents,
                     parent_node& current_node,
                     std::string_view prefix,
                     Range& childs,
                     std::optional<LeafValueType> leaf_value) {
    auto new_size = DirectoryTreeNode<LeafValueType>::calc_new_node_size(prefix.size(), std::ranges::size(childs),
                                                                         leaf_value.has_value());
    if (new_size == current_node.allocated_size()) {
      return true;
    }
    // Need to realloc
    parent_node new_node;
    auto new_offset = Alloc(new_size);
    if (!new_offset) {
      if (new_size > current_node.allocated_size())
        return false;
      // Slow path: we can shrink the current allocation
      Shrink(current_node.offset(), current_node.allocated_size(), new_size);
      new_node = dir_tree_node_ref<LeafValueType>::create(block().get(), current_node.offset(), new_size);
      return true;
    } else {
      new_node = dir_tree_node_ref<LeafValueType>::create(block().get(), new_offset, new_size);
    }
    if (parents.size())
      (*parents.back().iterator).set_value(new_offset);
    else
      mutable_extra_header()->root = new_offset;
    init_new_node(new_node, prefix, childs, leaf_value);
    Free(current_node.offset());
    current_node = new_node;
    return true;
  }

  void merge_empty_node(std::deque<typename iterator::parent_node_info>& parents,
                        parent_node& current_node,
                        bool reallocate = true) {
    assert(current_node.size() == 1 && !current_node.has_leaf());
    auto val = *current_node.begin();
    parent_node child{dir_tree_node_ref<LeafValueType>::load(block().get(), val.value())};
    std::string new_prefix = current_node.prefix();
    new_prefix += val.key();
    new_prefix += child.prefix();
    if (child.set_prefix(new_prefix)) {
      Free(current_node.offset());
      if (parents.size())
        (*parents.back().iterator).set_value(val.value());
      else
        mutable_extra_header()->root = val.value();
      return;
    } else if (recreate_node(parents, child, new_prefix, child, child.leaf())) {
      // We gave our parents, so it already updated the new offset.
      Free(current_node.offset());
      return;
    } else if (!reallocate) {
      // This parameter should be given only in situation where it will succeed.
      assert(false);
      return;
    }
    // Ok we failed to alloc, we need to reallocate the whole tree.
    std::deque<typename iterator::parent_node_info> old_parents, new_parents;
    parent_node root_node{dir_tree_node_ref<LeafValueType>::load(block().get(), extra_header()->root.value())};
    // TODO: detach and reinitialize tree
    DirectoryTree new_tree;
    new_tree.Init();
    merge_copy(new_tree, old_parents, new_parents, root_node, current_node);
  }

  void split_copy(DirectoryTree& new_tree,
                  std::deque<typename iterator::parent_node_info>& new_parents,
                  std::deque<typename iterator::parent_node_info>& parents,
                  const parent_node& node,
                  const std::deque<typename iterator::parent_node_info>& split_parents,
                  bool left) {
    auto start = node.begin();
    auto end = node.end();
    auto leaf = node.leaf();
    if (parents.size() < split_parents.size() && split_parents[parents.size()].node == node) {
      if (left) {
        end = split_parents[parents.size()].iterator + 1;
      } else {
        start = split_parents[parents.size()].iterator;
        leaf = std::nullopt;
      }
    } else if (left && parents.size() == split_parents.size() && parents.back().node == split_parents.back().node &&
               parents.back().iterator == split_parents.back().iterator) {
      // Just copy the last value
      assert(leaf.has_value());
      end = start;
    }
    auto new_node = *new_tree.alloc_new_node(node.prefix(), std::ranges::subrange(start, end), leaf);
    if (leaf) {
      copy_value(new_tree, new_node, *leaf);
      new_tree.extra_header()->records_count += 1;
    }
    if (new_parents.empty()) {
      new_tree.extra_header()->root = new_node.offset();
    } else {
      *(new_parents.back().iterator).set_value(new_node.offset());
    }
    int i = 0;
    for (auto it = start; it != end; ++it) {
      parents.push_back({node, it});
      new_parents.push_back({new_node, new_node.begin() + i++});
      split_copy(new_tree, new_parents, parents, {dir_tree_node_ref<LeafValueType>::load(block().get(), (*it).value())},
                 split_parents, left);
      parents.pop_back();
      new_parents.pop_back();
    }
    if (!new_node.has_leaf() && new_node.size() == 1) {
      new_tree.merge_empty_node(new_parents, new_node, /*rellocate=*/false);
    }
  }

  void merge_copy(DirectoryTree& new_tree,
                  std::deque<typename iterator::parent_node_info>& new_parents,
                  std::deque<typename iterator::parent_node_info>& parents,
                  const parent_node& node,
                  const parent_node& merge_node,
                  std::string_view merge_prefix = {}) {
    if (merge_node == node) {
      assert(!node.has_leaf() && node.size() == 1);
      std::string merge_prefix;
      merge_prefix += node.prefix();
      merge_prefix += (*node.begin()).key();
      parents.push_back({node, node.begin()});
      merge_copy(new_tree, new_parents, parents,
                 {dir_tree_node_ref<LeafValueType>::load(block().get(), (*node.begin()).value())}, merge_node,
                 merge_prefix);
      parents.pop_back();
      return;
    }
    auto leaf = node.leaf();
    auto new_node =
        *new_tree.alloc_new_node(merge_prefix.empty() ? node.prefix() : merge_prefix + node.prefix(), node, leaf);
    if (leaf) {
      copy_value(new_tree, new_node, *leaf);
      new_tree.extra_header()->records_count += 1;
    }
    if (new_parents.empty()) {
      new_tree.extra_header()->root = new_node.offset();
    } else {
      *(new_parents.back().iterator).set_value(new_node.offset());
    }
    int i = 0;
    for (auto it = node.begin(); it != node.end(); ++it) {
      parents.push_back({node, it});
      new_parents.push_back({new_node, new_node.begin() + i++});
      merge_copy(new_tree, new_parents, parents, {dir_tree_node_ref<LeafValueType>::load(block().get(), (*it).value())},
                 merge_node);
      parents.pop_back();
      new_parents.pop_back();
    }
  }
};
