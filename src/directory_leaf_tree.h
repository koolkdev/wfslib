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
  DirectoryLeafTree(std::shared_ptr<Block> block) : DirectoryTree(std::move(block)) {}
};
