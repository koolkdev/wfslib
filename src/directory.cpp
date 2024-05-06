/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory.h"

#include <numeric>
#include <utility>

#include "file.h"
#include "quota_area.h"
#include "structs.h"

Directory::Directory(std::string name,
                     AttributesRef attributes,
                     std::shared_ptr<QuotaArea> quota,
                     std::shared_ptr<Block> block)
    : WfsItem(std::move(name), std::move(attributes)),
      quota_(std::move(quota)),
      block_(std::move(block)),
      map_{quota_, block_} {}

std::expected<std::shared_ptr<WfsItem>, WfsError> Directory::GetObject(const std::string& name) const {
  try {
    // TODO: Case insensitive
    auto it = find(name);
    if (it.is_end()) {
      return std::unexpected(WfsError::kItemNotFound);
    }
    return (*it).item;
  } catch (const WfsException& e) {
    return std::unexpected(e.error());
  }
}

std::expected<std::shared_ptr<Directory>, WfsError> Directory::GetDirectory(const std::string& name) const {
  auto obj = GetObject(name);
  if (!obj.has_value())
    return std::unexpected(obj.error());
  if (!(*obj)->is_directory()) {
    // Not a directory
    return std::unexpected(kNotDirectory);
  }
  return std::dynamic_pointer_cast<Directory>(*obj);
}

std::expected<std::shared_ptr<File>, WfsError> Directory::GetFile(const std::string& name) const {
  auto obj = GetObject(name);
  if (!obj.has_value())
    return std::unexpected(obj.error());
  if (!(*obj)->is_file()) {
    // Not a file
    return std::unexpected(kNotFile);
  }
  return std::dynamic_pointer_cast<File>(*obj);
}

Directory::iterator Directory::find(std::string key) const {
  // to lowercase
  std::ranges::transform(key, key.begin(), [](char c) { return std::tolower(c); });
  return {map_.find(key)};
}
