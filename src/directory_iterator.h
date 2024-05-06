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
#include "errors.h"

class QuotaArea;
class WfsItem;

struct DiretoryItem {
  std::string name;
  std::expected<std::shared_ptr<WfsItem>, WfsError> item;
};

class DirectoryIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = ptrdiff_t;

  using value_type = DiretoryItem;
  using ref_type = DiretoryItem;

  using reference = ref_type;

  using parent_node_info = dir_node_iterator_info<DirectoryParentTree>;
  using leaf_node_info = dir_node_iterator_info<DirectoryLeafTree>;

  DirectoryIterator() = default;
  DirectoryIterator(std::shared_ptr<QuotaArea> quota, std::deque<parent_node_info> parents, leaf_node_info leaf);

  reference operator*() const;

  DirectoryIterator& operator++();
  DirectoryIterator& operator--();
  DirectoryIterator operator++(int);
  DirectoryIterator operator--(int);

  bool operator==(const DirectoryIterator& other) const { return leaf_.iterator == other.leaf_.iterator; }

  std::deque<parent_node_info>& parents() { return parents_; };
  const std::deque<parent_node_info>& parents() const { return parents_; };
  leaf_node_info& leaf() { return leaf_; };
  const leaf_node_info& leaf() const { return leaf_; };

  bool is_begin() const;
  bool is_end() const { return leaf_.iterator.is_end(); }

  Block::DataRef<Attributes> attributes() const { return {leaf_.node.block(), uint16_t{(*leaf_.iterator).value()}}; }

 private:
  std::shared_ptr<QuotaArea> quota_;
  std::deque<parent_node_info> parents_;
  leaf_node_info leaf_;
};
