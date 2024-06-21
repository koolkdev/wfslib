/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "block.h"
#include "directory_leaf_tree.h"
#include "directory_parent_tree.h"

class QuotaArea;
struct Attributes;

struct DiretoryMapItem {
  std::string name;
  Block::DataRef<Attributes> attributes;
};

class DirectoryMapIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = ptrdiff_t;

  using value_type = DiretoryMapItem;
  using ref_type = DiretoryMapItem;

  using reference = ref_type;

  using parent_node_info = dir_node_iterator_info<DirectoryParentTree>;
  using leaf_node_info = dir_node_iterator_info<DirectoryLeafTree>;

  DirectoryMapIterator() = default;
  DirectoryMapIterator(std::shared_ptr<QuotaArea> quota, std::vector<parent_node_info> parents, leaf_node_info leaf);

  reference operator*() const;

  DirectoryMapIterator& operator++();
  DirectoryMapIterator& operator--();
  DirectoryMapIterator operator++(int);
  DirectoryMapIterator operator--(int);

  bool operator==(const DirectoryMapIterator& other) const { return leaf_.iterator == other.leaf_.iterator; }

  std::vector<parent_node_info>& parents() { return parents_; };
  const std::vector<parent_node_info>& parents() const { return parents_; };
  leaf_node_info& leaf() { return leaf_; };
  const leaf_node_info& leaf() const { return leaf_; };

  const std::shared_ptr<QuotaArea>& quota() const { return quota_; };

  bool is_begin() const;
  bool is_end() const { return leaf_.iterator.is_end(); }

 private:
  std::shared_ptr<QuotaArea> quota_;
  std::vector<parent_node_info> parents_;
  leaf_node_info leaf_;
};
