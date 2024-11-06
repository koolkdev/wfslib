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
  using type = dir_parent_tree_value_type;
  using base = DirectoryTree<type>;

  DirectoryParentTree() = default;
  DirectoryParentTree(std::shared_ptr<Block> block) : DirectoryTree(std::move(block)) {}

  void split(DirectoryTree& left, DirectoryTree& right, const iterator& pos) const override;
  void erase(iterator& pos) override;

  bool can_erase(iterator& pos) const;

 protected:
  void copy_value(DirectoryTree&, parent_node&, type) const override {}
  std::shared_ptr<base> create(std::shared_ptr<Block> block) const override {
    return std::make_shared<DirectoryParentTree>(std::move(block));
  }
};
