/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "ftree.h"

template <>
PTreeNode<FTreeLeaf_details>::iterator split_point(const PTreeNode<FTreeLeaf_details>& node,
                                                   const typename PTreeNode<FTreeLeaf_details>::iterator& pos,
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
  split_key = (*res).key;
  return res;
}
