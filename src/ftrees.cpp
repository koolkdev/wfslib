/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "ftrees.h"

template <>
PTreeNode<FTreeLeaf_details>::const_iterator split_point(
    const PTreeNode<FTreeLeaf_details>& node,
    const typename PTreeNode<FTreeLeaf_details>::const_iterator& pos,
    key_type& split_key) {
  assert(node.begin() <= pos && pos <= node.end());
  assert(node.full());
  auto res = pos;
  switch (pos - node.begin()) {
    case 0:
    case 1:
    case 2:
    case 3:
      res = node.begin() + 3;
      break;
    case 4:
      return pos;
    case 5:
    case 6:
    case 7:
      res = node.begin() + 4;
      break;
  }
  split_key = res->key;
  return res;
}

void FTrees::split(FTrees& left, FTrees& right, key_type& split_point_key) {
  // Find the ftree with most items
  auto max_ftree_it = std::ranges::max_element(ftrees_, [](const FTree& a, const FTree& b) {
    return a.header()->items_count.value() < b.header()->items_count.value();
  });
  split_point_key = max_ftree_it->middle()->key;
  for (auto [old_ftree, left_ftree, right_ftree] : std::views::zip(ftrees_, left.ftrees_, right.ftrees_)) {
    old_ftree.split_compact(left_ftree, right_ftree, old_ftree.find(split_point_key, false));
  }
}
