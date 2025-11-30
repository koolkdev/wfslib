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
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    while (true) {
      parent_node parent{dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)};
      if (parent.has_leaf()) {
        return {block().get(), std::move(parents), parent.leaf_ref()};
      }
      parents.emplace_back(parent, parent.begin());
      node_offset = (*parents.back().iterator).value();
    }
  }

  iterator end() const {
    if (size() == 0)
      return {block().get(), {}, {}};
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    while (true) {
      parents.emplace_back(dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset));
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
    std::optional<parent_node> new_node;
    if (size() == 0) {
      new_node = alloc_new_node<>(key_val.key, {}, key_val.value);
      if (!new_node) {
        assert(false);
        return false;
      }
      mutable_extra_header()->root = static_cast<uint16_t>(new_node->offset());
      mutable_extra_header()->records_count += 1;
      return true;
    }

    // Find the position to insert at
    std::optional<typename iterator::parent_node_info> last_parent;
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
          if (!parent.set_leaf(key_val.value)) {
            if (!recreate_node(last_parent, parent, parent.prefix(), parent, key_val.value)) {
              return false;
            }
          }
          mutable_extra_header()->records_count += 1;
          return true;
        }
      } else if (prefix_it == prefix.end()) {
        // Got to end of prefix, but there is still more key
        auto iterator = parent.find(*key_it, /*exact_match=*/false);
        if (iterator != parent.end()) {
          if ((*iterator).key() == *key_it) {
            // Found match to the next key char
            current_key = key_it + 1;
            node_offset = (*iterator).value();
            last_parent.emplace(parent, iterator);
            continue;
          } else if ((*iterator).key() < *key_it) {
            ++iterator;
          }
        }
        // Insert new key to parent
        new_node = alloc_new_node({key_it + 1, key_val.key.end()}, {}, key_val.value);
        if (!new_node) {
          return false;
        }
        if (!parent.insert(iterator, {*key_it, new_node->offset()})) {
          auto vals = std::ranges::to<std::vector<dir_tree_parent_node_item>>(parent);
          vals.insert(vals.begin() + (iterator - parent.begin()), {*key_it, new_node->offset()});
          if (!recreate_node(last_parent, parent, prefix, vals, parent.leaf())) {
            Free(new_node->offset(), new_node->allocated_size());
            return false;
          }
        }
        mutable_extra_header()->records_count += 1;
        return true;
      } else {
        new_node = alloc_new_node({key_it + 1, key_val.key.end()}, {}, key_val.value);
        if (!new_node) {
          return false;
        }
      }
      // Need to split prefix
      auto new_child = alloc_new_node({prefix_it + 1, prefix.end()}, parent, parent.leaf());
      if (!new_child) {
        if (new_node) {
          Free(new_node->offset(), new_node->allocated_size());
        }
        return false;
      }
      std::string_view new_prefix{prefix.begin(), prefix_it};
      std::vector<typename DirectoryTreeNode<LeafValueType>::iterator::value_type> new_nodes;
      new_nodes.emplace_back(*prefix_it, new_child->offset());
      std::optional<LeafValueType> new_leaf;
      if (new_node) {
        if (*prefix_it < *key_it) {
          new_nodes = {new_nodes[0], {*key_it, new_node->offset()}};
        } else {
          new_nodes = {{*key_it, new_node->offset()}, new_nodes[0]};
        }
      } else {
        // Full match to new prefix, new leaf.
        new_leaf = key_val.value;
      }
      if (!recreate_node(last_parent, parent, {prefix.begin(), prefix_it}, new_nodes, new_leaf)) {
        if (new_node) {
          Free(new_node->offset(), new_node->allocated_size());
        }
        Free(new_child->offset(), new_child->allocated_size());
        return false;
      }
      mutable_extra_header()->records_count += 1;
      return true;
    }
  }

  virtual void erase(iterator& pos) {
    // Remove current parent leaf
    auto parents = pos.parents();
    std::optional<typename iterator::parent_node_info> last_parent;
    if (!parents.empty())
      last_parent = parents.back();
    parent_node current_parent{pos.leaf().get_node()};
    if (current_parent.size() > 0) {
      if (!current_parent.remove_leaf()) {
        if (!recreate_node(last_parent, current_parent, current_parent.prefix(), current_parent, std::nullopt)) {
          assert(false);  // Always can shrink
          return;
        }
      }
    }
    mutable_extra_header()->records_count -= 1;
    while (true) {
      if (current_parent.size() > 1) {
        return;
      } else if (current_parent.size() == 1) {
        // Can merge with child node
        merge_empty_node(last_parent, current_parent);
        return;
      }
      // Empty node, remove
      Free(current_parent.offset(), current_parent.allocated_size());
      if (!last_parent) {
        // Removed last node
        assert(size() == 0);
        return;
      }
      auto parent = parents.back();
      parents.pop_back();
      if (parents.empty())
        last_parent.reset();
      else
        last_parent = parents.back();
      current_parent = parent.node;
      if (!current_parent.erase(parent.iterator)) {
        auto vals = std::ranges::to<std::vector<dir_tree_parent_node_item>>(current_parent);
        vals.erase(vals.begin() + (parent.iterator - parent.node.begin()));
        if (!recreate_node(last_parent, current_parent, current_parent.prefix(), vals, current_parent.leaf())) {
          assert(false);  // Always can shrink
          return;
        }
      }
      if (current_parent.has_leaf())
        return;
    }
  }

  virtual void split(DirectoryTree& left, DirectoryTree& right, const iterator& pos) const {
    // Implemented in a totally different way then original because it is way too complex for no reason.
    parent_node root_node{dir_tree_node_ref<LeafValueType>::load(block().get(), extra_header()->root.value())};
    split_copy(right, std::nullopt, root_node, pos.parents(), /*left=*/false);
    if (!pos.is_begin()) {
      auto end_left = pos;
      --end_left;
      split_copy(left, std::nullopt, root_node, end_left.parents(), /*left=*/true);
    }
  }

  iterator find(std::string_view key, bool exact_match = true) const {
    if (size() == 0)
      return end();
    std::vector<typename iterator::parent_node_info> parents;
    uint16_t node_offset = extra_header()->root.value();
    auto current_key = key.begin();
    while (true) {
      parent_node parent{dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)};
      auto prefix = parent.prefix();
      auto [key_it, prefix_it] = std::ranges::mismatch(std::ranges::subrange(current_key, key.end()), prefix);
      std::strong_ordering status = std::strong_ordering::less;
      if (key_it == key.end()) {
        // Got to end of our key
        if (prefix_it == prefix.end() && parent.has_leaf()) {
          // Found exact match!
          return {block().get(), std::move(parents), parent.leaf_ref()};
        }
      } else if (prefix_it == prefix.end()) {
        // We matched the prefix
        auto iterator = parent.find(*key_it, exact_match);
        if (iterator != parent.end() && *key_it >= (*iterator).key()) {
          current_key = key_it + 1;
          node_offset = (*iterator).value();
          parents.emplace_back(parent, iterator);
          if ((*iterator).key() != *key_it) {
            status = std::strong_ordering::greater;
            parent = {dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset)};
          } else {
            continue;
          }
        } else if (parent.has_leaf()) {
          status = std::strong_ordering::equal;
        }
      } else if (*key_it >= *prefix_it) {
        status = std::strong_ordering::greater;
      }

      if (exact_match) {
        return end();
      }

      if (status != std::strong_ordering::greater) {
        // Go to the next leaf and back
        while (!parent.has_leaf()) {
          parents.emplace_back(parent, parent.begin());
          node_offset = (*parents.back().iterator).value();
          parent = dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset);
        }
        iterator res{block().get(), std::move(parents), parent.leaf_ref()};
        if (status == std::strong_ordering::less && res != begin())
          --res;
        return res;
      } else {
        // Go to last leaf from here.
        while (parent.size()) {
          parents.emplace_back(parent, --parent.end());
          node_offset = (*parents.back().iterator).value();
          parent = dir_tree_node_ref<LeafValueType>::load(block().get(), node_offset);
        }
        assert(parent.has_leaf());
        return {block().get(), std::move(parents), parent.leaf_ref()};
      }
    }
  }

  virtual void Init(bool is_root) {
    SubBlockAllocator<DirectoryTreeHeader>::Init();
    auto* header = block()->template get_mutable_object<MetadataBlockHeader>(0);
    header->block_flags |= MetadataBlockHeader::Flags::DIRECTORY;
    if (is_root) {
      header->block_flags |= MetadataBlockHeader::Flags::DIRECTORY_ROOT_TREE;
    }
  }

 protected:
  virtual void copy_value(DirectoryTree& new_tree, parent_node& new_node, LeafValueType value) const = 0;
  virtual std::shared_ptr<DirectoryTree<LeafValueType>> create(std::shared_ptr<Block> block) const = 0;

 private:
  template <typename Range>
  void init_new_node(parent_node node,
                     std::string_view prefix,
                     const Range& childs,
                     std::optional<LeafValueType> leaf_value) {
    node.clear();
    node.set_prefix(prefix, /*check_size=*/false);
    if (leaf_value.has_value()) {
      node.set_leaf(*leaf_value, /*check_size=*/false);
    }
    node.insert(node.begin(), childs, /*check_size=*/false);
    assert(node.allocated_size() == dir_tree_node_ref<LeafValueType>::get_node_size(node.node()));
  }

  template <typename Range = std::vector<typename DirectoryTreeNode<LeafValueType>::iterator::value_type>>
  std::optional<parent_node> alloc_new_node(std::string_view prefix,
                                            const Range& childs,
                                            std::optional<LeafValueType> leaf_value) {
    auto new_size = static_cast<uint16_t>(
        DirectoryTreeNode<LeafValueType>::calc_new_node_size(prefix.size(), childs.size(), leaf_value.has_value()));
    auto new_offset = Alloc(new_size);
    if (!new_offset.has_value()) {
      return std::nullopt;
    }
    parent_node new_node = dir_tree_node_ref<LeafValueType>::create(block().get(), *new_offset, new_size);
    init_new_node(new_node, prefix, childs, leaf_value);
    return new_node;
  }

  template <typename Range>
  bool recreate_node(std::optional<typename iterator::parent_node_info> parent,
                     parent_node& current_node,
                     std::string_view prefix,
                     const Range& childs,
                     std::optional<LeafValueType> leaf_value) {
    auto new_size = static_cast<uint16_t>(DirectoryTreeNode<LeafValueType>::calc_new_node_size(
        prefix.size(), std::ranges::size(childs), leaf_value.has_value()));
    if (new_size == current_node.allocated_size()) {
      init_new_node(current_node, prefix, childs, leaf_value);
      return true;
    }
    // Need to realloc
    parent_node new_node;
    auto new_offset = Alloc(new_size);
    if (!new_offset.has_value()) {
      if (new_size > current_node.allocated_size())
        return false;
      // Slow path: we can shrink the current allocation
      Shrink(current_node.offset(), current_node.allocated_size(), new_size);
      current_node = dir_tree_node_ref<LeafValueType>::create(block().get(), current_node.offset(), new_size);
      init_new_node(current_node, prefix, childs, leaf_value);
      return true;
    } else {
      new_node = dir_tree_node_ref<LeafValueType>::create(block().get(), *new_offset, new_size);
    }
    if (parent)
      (*parent->iterator).set_value(*new_offset);
    else
      mutable_extra_header()->root = *new_offset;
    init_new_node(new_node, prefix, childs, leaf_value);
    Free(current_node.offset(), current_node.allocated_size());
    current_node = new_node;
    return true;
  }

  void merge_empty_node(std::optional<typename iterator::parent_node_info> parent,
                        parent_node& current_node,
                        bool reallocate = true) {
    assert(current_node.size() == 1 && !current_node.has_leaf());
    auto val = *current_node.begin();
    auto new_offset = val.value();
    parent_node child{dir_tree_node_ref<LeafValueType>::load(block().get(), new_offset)};
    std::string new_prefix = std::ranges::to<std::string>(current_node.prefix());
    new_prefix += val.key();
    new_prefix += std::ranges::to<std::string>(child.prefix());
    if (child.set_prefix(new_prefix)) {
      Free(current_node.offset(), current_node.allocated_size());
      if (parent)
        (*parent->iterator).set_value(new_offset);
      else
        mutable_extra_header()->root = new_offset;
      return;
    } else if (recreate_node(parent, child, new_prefix, child, child.leaf())) {
      // We gave our parents, so it already updated the new offset.
      Free(current_node.offset(), current_node.allocated_size());
      return;
    } else if (!reallocate) {
      // This parameter should be given only in situation where it will succeed.
      assert(false);
      return;
    }
    // Ok we failed to alloc, we need to reallocate the whole tree.
    auto old_tree = create(Block::CreateDetached(std::ranges::to<std::vector>(block()->data())));
    parent_node root_node{
        dir_tree_node_ref<LeafValueType>::load(old_tree->block().get(), extra_header()->root.value())};
    parent_node merge_node{dir_tree_node_ref<LeafValueType>::load(
        old_tree->block().get(), parent ? (*parent->iterator).value() : extra_header()->root.value())};
    Init(block()->template get_object<MetadataBlockHeader>(0)->block_flags.value() &
         MetadataBlockHeader::Flags::DIRECTORY_ROOT_TREE);
    old_tree->merge_copy(*this, root_node, merge_node);
  }

  void split_copy(DirectoryTree& new_tree,
                  std::optional<typename iterator::parent_node_info> parent,
                  const parent_node& node,
                  const std::vector<typename iterator::parent_node_info>& split_parents,
                  bool left,
                  size_t depth = 0,
                  std::optional<typename iterator::parent_node_info> new_parent = std::nullopt) const {
    auto start = node.begin();
    auto end = node.end();
    auto leaf = node.leaf();
    if (depth < split_parents.size() && split_parents[depth].node == node) {
      if (left) {
        end = split_parents[depth].iterator + 1;
      } else {
        start = split_parents[depth].iterator;
        leaf = std::nullopt;
      }
    } else if (left && parent && depth == split_parents.size() && parent->node == split_parents.back().node &&
               parent->iterator == split_parents.back().iterator) {
      // Just copy the last value
      assert(leaf.has_value());
      end = start;
    }
    auto new_node = *new_tree.alloc_new_node(node.prefix(), std::ranges::subrange(start, end), leaf);
    if (leaf) {
      copy_value(new_tree, new_node, *leaf);
      new_tree.mutable_extra_header()->records_count += 1;
    }
    if (new_parent) {
      (*new_parent->iterator).set_value(new_node.offset());
    } else {
      new_tree.mutable_extra_header()->root = new_node.offset();
    }
    int i = 0;
    for (auto it = start; it != end; ++it) {
      split_copy(new_tree, typename iterator::parent_node_info{node, it},
                 {dir_tree_node_ref<LeafValueType>::load(block().get(), (*it).value())}, split_parents, left, depth + 1,
                 typename iterator::parent_node_info{new_node, new_node.begin() + i++});
    }
    if (!new_node.has_leaf() && new_node.size() == 1) {
      new_tree.merge_empty_node(new_parent, new_node, /*rellocate=*/false);
    }
  }

  void merge_copy(DirectoryTree& new_tree,
                  const parent_node& node,
                  const parent_node& merge_node,
                  std::optional<typename iterator::parent_node_info> new_parent = std::nullopt,
                  std::string_view merge_prefix = {}) {
    if (merge_node == node) {
      assert(!node.has_leaf() && node.size() == 1);
      std::string new_merge_prefix = std::ranges::to<std::string>(node.prefix());
      new_merge_prefix += (*node.begin()).key();
      merge_copy(new_tree, {dir_tree_node_ref<LeafValueType>::load(block().get(), (*node.begin()).value())}, merge_node,
                 new_parent, new_merge_prefix);
      return;
    }
    auto leaf = node.leaf();
    auto new_node = *new_tree.alloc_new_node(
        merge_prefix.empty()
            ? node.prefix()
            : std::ranges::to<std::string>(merge_prefix) + std::ranges::to<std::string>(node.prefix()),
        node, leaf);
    if (leaf) {
      copy_value(new_tree, new_node, *leaf);
      new_tree.mutable_extra_header()->records_count += 1;
    }
    if (new_parent) {
      (*new_parent->iterator).set_value(new_node.offset());
    } else {
      new_tree.mutable_extra_header()->root = new_node.offset();
    }
    int i = 0;
    for (auto it = node.begin(); it != node.end(); ++it) {
      merge_copy(new_tree, {dir_tree_node_ref<LeafValueType>::load(block().get(), (*it).value())}, merge_node,
                 typename iterator::parent_node_info{new_node, new_node.begin() + i++});
    }
  }
};
