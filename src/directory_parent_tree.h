/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "directory_tree.h"

class DirectoryParentTree : public DirectoryTree<dir_parent_tree_value_type> {
 public:
  using type = dir_parent_tree_value_type;
  using base = DirectoryTree<type>;

  DirectoryParentTree() = default;
  DirectoryParentTree(std::shared_ptr<Block> block) : DirectoryTree(std::move(block)) {}

  // TODO: Move to .cc
  void split(DirectoryTree& left, DirectoryTree& right, const iterator& pos) const override {
    assert(pos != begin());
    assert(pos != end());
    base::split(left, right, pos);
    // The first key must always be empty
    // They implemented it differently but this is more simple...
    auto first = right.begin();
    auto first_value = (*first).value();
    right.erase(first);
    [[maybe_unused]] auto res = right.insert({"", first_value});
    assert(res);
  }

  bool can_erase(iterator& pos) const {
    if (pos.parents().empty())
      return true;
    auto last_parent = pos.parents().end();
    --last_parent;
    if (last_parent->node.size() > 1)
      return true;
    if (last_parent->node.size() != 1) {
      // size == 0, check if need to merge at our parent
      if (last_parent == pos.parents().begin())
        return true;
      --last_parent;
      if (last_parent->node.size() != 1)
        return true;
    }
    // merge flow, be on the safest size and check if we can alloc the new size node.
    // They do a different calculation with the total free bytes but I am not sure that it is correct and we won't take
    // risk here.
    auto merged_child_node = dir_tree_node_ref<type>::load(block().get(), (*last_parent->node.begin()).value());
    auto new_size = static_cast<uint16_t>(DirectoryTreeNode<type>::calc_new_node_size(
        last_parent->node.prefix().size() + merged_child_node->prefix_length.value() + 1,
        merged_child_node->keys_count.value() - merged_child_node.has_leaf_value(),
        merged_child_node.has_leaf_value()));
    return new_size == merged_child_node.node_size || CanAlloc(new_size);
  }

  void erase(iterator& pos) override {
    if (pos.is_begin()) {
      // The first key must always be empty, so lets keep it like that.
      assert((*pos).key().empty());
      auto next = pos;
      ++next;
      if (!next.is_end()) {
        (*pos).set_value((*next).value());
        base::erase(next);
        return;
      }
    }
    base::erase(pos);
  }

 protected:
  void copy_value(DirectoryTree&, parent_node&, type) const override {}
  std::shared_ptr<base> create(std::shared_ptr<Block> block) const override {
    return std::make_shared<DirectoryParentTree>(std::move(block));
  }
};
