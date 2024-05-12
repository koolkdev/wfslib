/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "directory_tree.h"

class DirectoryLeafTree : public DirectoryTree<dir_leaf_tree_value_type> {
 public:
  DirectoryLeafTree() = default;
  DirectoryLeafTree(std::shared_ptr<Block> block) : DirectoryTree(std::move(block)) {}

 private:
  void copy_value(DirectoryTree& new_tree, parent_node& new_node, dir_leaf_tree_value_type value) override {
    Block::RawDataRef<Attributes> attributes{block().get(), value};
    auto size = 1 << attributes.get()->entry_log2_size.value();
    auto new_offset = new_tree.Alloc(static_cast<uint16_t>(size));
    Block::RawDataRef<Attributes> new_attributes{new_tree.block().get(), new_offset};
    std::memcpy(new_attributes.get_mutable(), attributes.get(), size);
    new_node.set_leaf(new_offset);
  }
};
