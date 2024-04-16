/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <expected>
#include <memory>
#include <string>

#include "directory_items_iterator.h"
#include "errors.h"
#include "wfs_item.h"

class Area;
class Block;
class File;

struct DirectoryTreeNode;

class Directory : public WfsItem, public std::enable_shared_from_this<Directory> {
 public:
  // TODO: Replace name with tree iterator?
  Directory(const std::string& name,
            AttributesBlock attributes,
            const std::shared_ptr<Area>& area,
            const std::shared_ptr<Block>& block)
      : WfsItem(name, attributes), area_(area), block_(block) {}

  std::expected<std::shared_ptr<WfsItem>, WfsError> GetObject(const std::string& name) const;
  std::expected<std::shared_ptr<Directory>, WfsError> GetDirectory(const std::string& name) const;
  std::expected<std::shared_ptr<File>, WfsError> GetFile(const std::string& name) const;

  size_t Size() const;
  DirectoryItemsIterator begin() const;
  DirectoryItemsIterator end() const;

  const std::shared_ptr<Area>& area() const { return area_; }

  static void LoadDirectory(std::shared_ptr<Area> area, AttributesBlock attributes, uint32_t block_number);

 private:
  friend DirectoryItemsIterator;
  friend class Recovery;

  // TODO: We may have cyclic reference here if we do cache in area.
  std::shared_ptr<Area> area_;

  std::shared_ptr<Block> block_;

  std::expected<std::shared_ptr<WfsItem>, WfsError> GetObjectInternal(const std::string& name,
                                                                      const AttributesBlock& attributes) const;
  std::expected<AttributesBlock, WfsError> GetObjectAttributes(const std::shared_ptr<Block>& block,
                                                               const std::string& name) const;
};
