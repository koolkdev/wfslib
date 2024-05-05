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
PTreeNode<FTreeLeaf_details>::iterator split_point(const PTreeNode<FTreeLeaf_details>& node,
                                                   const typename PTreeNode<FTreeLeaf_details>::iterator& pos,
                                                   key_type& split_key);

static_assert(sizeof(PTreeNode_details) == sizeof(FTreeLeaf_details));
using FTreesBlockArgs = TreeNodesAllocatorArgs<FTreesBlockHeader, FTreesFooter, sizeof(PTreeNode_details)>;
using FTreesBlock = TreeNodesAllocator<FTreesBlockArgs>;

class FTree : public PTree<PTreeNode_details, FTreeLeaf_details, FTreesBlockArgs> {
 public:
  FTree() = default;
  FTree(std::shared_ptr<Block> block, size_t block_size_index)
      : PTree<PTreeNode_details, FTreeLeaf_details, FTreesBlockArgs>(std::move(block)),
        block_size_index_(block_size_index) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->trees[block_size_index_]; }
  const PTreeHeader* header() const override { return &tree_header()->trees[block_size_index_]; }

  size_t index() const { return block_size_index_; }

 private:
  size_t block_size_index_;
};
static_assert(std::ranges::bidirectional_range<FTree>);
