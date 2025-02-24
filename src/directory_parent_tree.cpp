/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_parent_tree.h"

void DirectoryParentTree::split(DirectoryTree& left, DirectoryTree& right, const iterator& pos) const {
  assert(pos != begin());
  assert(pos != end());
  base::split(left, right, pos);
  // The first key must always be empty
  // They implemented it differently but this is more simple...
  auto first = right.begin();
  auto first_value = (*first).value();
  right.base::erase(first);
  [[maybe_unused]] auto res = right.insert({"", first_value});
  assert(res);
}

void DirectoryParentTree::erase(iterator& pos) {
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

bool DirectoryParentTree::can_erase(iterator& pos) const {
  parent_node current_parent{pos.leaf().get_node()};
  if (current_parent.size() > 1)
    return true;
  if (current_parent.size() != 1) {
    // size == 0, check if need to merge at our parent
    if (pos.parents().empty())
      return true;
    current_parent = pos.parents().back().node;
    if (!current_parent.has_leaf() && current_parent.size() != 2)
      return true;
  }
  // merge flow, be on the safest size and check if we can alloc the new size node.
  // They do a different calculation with the total free bytes but I am not sure that it is correct and we won't take
  // risk here.
  auto merged_child_node = dir_tree_node_ref<type>::load(block().get(), (*current_parent.begin()).value());
  auto new_size = static_cast<uint16_t>(DirectoryTreeNode<type>::calc_new_node_size(
      current_parent.prefix().size() + merged_child_node->prefix_length.value() + 1,
      merged_child_node->keys_count.value() - merged_child_node.has_leaf_value(), merged_child_node.has_leaf_value()));
  return new_size == merged_child_node.node_size || CanAlloc(new_size);
}
