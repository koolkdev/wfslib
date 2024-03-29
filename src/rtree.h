/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include "ptree.h"
#include "structs.h"
#include "tree_nodes_allocator.h"

template <>
PTreeNode<RTreeLeaf_details>::const_iterator split_point(
    const PTreeNode<RTreeLeaf_details>& node,
    const typename PTreeNode<RTreeLeaf_details>::const_iterator& pos,
    key_type& split_key);

static_assert(sizeof(PTreeNode_details) == sizeof(RTreeLeaf_details));
using EPTreeBlock = TreeNodesAllocator<FreeBlocksAllocatorHeader, EPTreeFooter, sizeof(PTreeNode_details)>;

class RTree : public PTree<PTreeNode_details, RTreeLeaf_details, EPTreeBlock> {
 public:
  RTree(std::shared_ptr<MetadataBlock> block)
      : PTree<PTreeNode_details, RTreeLeaf_details, EPTreeBlock>(std::move(block)) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->current_tree; }
  const PTreeHeader* header() const override { return &tree_header()->current_tree; }

  void Init(uint8_t depth);
};
