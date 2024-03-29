/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "rtree.h"

template <>
PTreeNode<RTreeLeaf_details>::const_iterator split_point(
    const PTreeNode<RTreeLeaf_details>& node,
    const typename PTreeNode<RTreeLeaf_details>::const_iterator& pos,
    key_type& split_key) {
  assert(node.begin() <= pos && pos <= node.end());
  assert(node.full());
  auto res = pos;
  switch (pos - node.begin()) {
    case 0:
    case 1:
      res = node.begin() + 1;
      break;
    case 2:
      res = node.begin() + 2;
      break;
    case 3:
      return pos;
    case 4:
      res = node.begin() + 3;
      break;
  }
  split_key = res->key;
  return res;
}

void RTree::Init(uint8_t depth) {
  EPTreeBlock::Init();
  mutable_tree_header()->depth = depth;
  mutable_tree_header()->block_number = block_->BlockNumber();
}
