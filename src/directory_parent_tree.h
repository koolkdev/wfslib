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
  DirectoryParentTree() = default;
  DirectoryParentTree(std::shared_ptr<Block> block) : DirectoryTree(std::move(block)) {}

 private:
  void copy_value(DirectoryTree&, parent_node&, dir_parent_tree_value_type) override {}
};
