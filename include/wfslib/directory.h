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

#include "directory_iterator.h"
#include "errors.h"
#include "wfs_item.h"

class Area;
class Block;
class File;

class Directory : public WfsItem, public std::enable_shared_from_this<Directory> {
 public:
  using iterator = DirectoryIterator;

  // TODO: Replace name with tree iterator?
  Directory(std::string name, AttributesRef attributes, std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> block);

  std::expected<std::shared_ptr<WfsItem>, WfsError> GetObject(const std::string& name) const;
  std::expected<std::shared_ptr<Directory>, WfsError> GetDirectory(const std::string& name) const;
  std::expected<std::shared_ptr<File>, WfsError> GetFile(const std::string& name) const;

  size_t size() const { return CalcSizeOfDirectoryBlock(block_); }

  iterator begin() const;
  iterator end() const;

  iterator find(std::string_view key, bool exact_match = true) const;

  const std::shared_ptr<QuotaArea>& quota() const { return quota_; }

 private:
  friend class Recovery;

  size_t CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const;

  std::shared_ptr<QuotaArea> quota_;

  std::shared_ptr<Block> block_;
};
