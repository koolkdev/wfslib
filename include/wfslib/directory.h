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
#include "directory_map.h"
#include "entry.h"
#include "errors.h"

class Area;
class Block;
class File;

class Directory : public Entry, public std::enable_shared_from_this<Directory> {
 public:
  using iterator = DirectoryIterator;

  // TODO: Replace name with tree iterator?
  Directory(std::string name, MetadataRef metadata, std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> block);

  std::expected<std::shared_ptr<Entry>, WfsError> GetEntry(std::string_view name) const;
  std::expected<std::shared_ptr<Directory>, WfsError> GetDirectory(std::string_view name) const;
  std::expected<std::shared_ptr<File>, WfsError> GetFile(std::string_view name) const;

  size_t size() const { return map_.size(); }

  iterator begin() const { return {map_.begin()}; }
  iterator end() const { return {map_.end()}; }

  iterator find(std::string_view key) const;

  const std::shared_ptr<QuotaArea>& quota() const { return quota_; }

 private:
  friend class Recovery;

  std::shared_ptr<QuotaArea> quota_;
  std::shared_ptr<Block> block_;

  DirectoryMap map_;
};
