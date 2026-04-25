/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory.h"

#include <utility>

#include "file.h"
#include "quota_area.h"

Directory::Directory(std::string name,
                     MetadataRef metadata,
                     std::shared_ptr<QuotaArea> quota,
                     std::shared_ptr<Block> block)
    : Entry(std::move(name), std::move(metadata)),
      quota_(std::move(quota)),
      block_(std::move(block)),
      map_{quota_, block_} {}

std::expected<std::shared_ptr<Entry>, WfsError> Directory::GetEntry(std::string_view name) const {
  try {
    auto it = find(name);
    if (it.is_end()) {
      return std::unexpected(WfsError::kEntryNotFound);
    }
    return (*it).entry;
  } catch (const WfsException& e) {
    return std::unexpected(e.error());
  }
}

std::expected<std::shared_ptr<Directory>, WfsError> Directory::GetDirectory(std::string_view name) const {
  auto entry = GetEntry(name);
  if (!entry.has_value())
    return std::unexpected(entry.error());
  if (!(*entry)->is_directory()) {
    // Not a directory
    return std::unexpected(kNotDirectory);
  }
  return std::dynamic_pointer_cast<Directory>(*entry);
}

std::expected<std::shared_ptr<File>, WfsError> Directory::GetFile(std::string_view name) const {
  auto entry = GetEntry(name);
  if (!entry.has_value())
    return std::unexpected(entry.error());
  if (!(*entry)->is_file()) {
    // Not a file
    return std::unexpected(kNotFile);
  }
  return std::dynamic_pointer_cast<File>(*entry);
}

Directory::iterator Directory::find(std::string_view key) const {
  std::string lowercase_key{key};
  // to lowercase
  std::ranges::transform(lowercase_key, lowercase_key.begin(), [](char c) { return std::tolower(c); });
  return {map_.find(lowercase_key)};
}
