/*
 * Copyright (C) 2022 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <string>

class Directory;
class WfsItem;
class MetadataBlock;

struct DirectoryTreeNode;

class DirectoryItemsIterator : public std::iterator<std::input_iterator_tag, std::shared_ptr<WfsItem>> {
 public:
  struct NodeState {
    std::shared_ptr<MetadataBlock> block;
    DirectoryTreeNode* node;
    std::shared_ptr<NodeState> parent;
    size_t current_index;
    std::string path;
  };

  DirectoryItemsIterator(const std::shared_ptr<Directory>& directory, const std::shared_ptr<NodeState>& node_state);
  DirectoryItemsIterator(const DirectoryItemsIterator& mit);

  DirectoryItemsIterator& operator++();
  DirectoryItemsIterator operator++(int);
  bool operator==(const DirectoryItemsIterator& rhs) const;
  bool operator!=(const DirectoryItemsIterator& rhs) const;
  std::shared_ptr<WfsItem> operator*();

 private:
  std::shared_ptr<Directory> directory_;
  std::shared_ptr<NodeState> node_state_;
};
