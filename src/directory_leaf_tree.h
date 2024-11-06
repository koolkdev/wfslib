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
  using base = DirectoryTree<dir_leaf_tree_value_type>;

  DirectoryLeafTree() = default;
  DirectoryLeafTree(std::shared_ptr<Block> block) : DirectoryTree(std::move(block)) {}

  void Init(bool is_root) override;

 private:
  void copy_value(DirectoryTree& new_tree, parent_node& new_node, dir_leaf_tree_value_type value) const override;
  std::shared_ptr<DirectoryTree<dir_leaf_tree_value_type>> create(std::shared_ptr<Block> block) const override {
    return std::make_shared<DirectoryLeafTree>(std::move(block));
  }
};
