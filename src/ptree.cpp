/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "ptree.h"

template <>
PTreeNode<PTreeNode_details>::iterator split_point(const PTreeNode<PTreeNode_details>& node,
                                                   const typename PTreeNode<PTreeNode_details>::iterator& pos,
                                                   key_type& split_key) {
  assert(node.begin() <= pos && pos <= node.end());
  assert(node.full());
  auto res = pos;
  switch (pos - node.begin()) {
    case 0:
    case 1:
    case 2:
      res = node.begin() + 3;
      break;
    case 3:
      return pos + 1;
    case 4:
    case 5:
      res = node.begin() + 4;
      break;
  }
  split_key = (*res).key();
  return res;
}
