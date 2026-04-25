/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include "directory_map_iterator.h"
#include "entry.h"
#include "errors.h"

class DirectoryMap;
class QuotaArea;

class DirectoryEntryCache {
 public:
  std::expected<std::shared_ptr<Entry>, WfsError> Load(DirectoryMap& directory_map,
                                                       DirectoryMapIterator it,
                                                       std::shared_ptr<QuotaArea> quota);

  void MetadataUpdated(std::string_view name, Block::DataRef<EntryMetadata> metadata);
  void EntryRemoved(std::string_view name);
  void RefreshMetadataRefs(const DirectoryMap& directory_map);

 private:
  Entry::EntryHandlePtr HandleFor(std::string_view key, Block::DataRef<EntryMetadata> metadata);

  std::map<std::string, std::weak_ptr<Entry::EntryHandle>> handles_;
  std::map<std::string, std::weak_ptr<Entry>> entries_;
};
