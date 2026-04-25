/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_iterator.h"

#include "directory_map.h"
#include "entry.h"

DirectoryIterator::DirectoryIterator(DirectoryMapIterator base, std::shared_ptr<DirectoryMap> map)
    : base_(base), map_(std::move(map)) {}

DirectoryIterator::reference DirectoryIterator::operator*() const {
  auto val = *base_;
  auto name = val.metadata.get()->GetCaseSensitiveName(val.name);
  Entry::MetadataUpdater metadata_updater;
  Entry::MetadataRefresher metadata_refresher;
  if (map_) {
    auto updater_map = map_;
    auto updater_name = val.name;
    metadata_updater = [map = std::move(updater_map), map_name = std::move(updater_name)](
                           const EntryMetadata* metadata) { return map->replace_metadata(map_name, metadata); };
    auto refresher_map = map_;
    auto refresher_name = val.name;
    metadata_refresher = [map = std::move(refresher_map),
                          map_name = std::move(refresher_name)]() -> std::expected<Entry::MetadataRef, WfsError> {
      auto it = map->find(map_name);
      if (it.is_end())
        return std::unexpected(WfsError::kEntryNotFound);
      return (*it).metadata;
    };
  }
  return {name,
          Entry::Load(base_.quota(), name, val.metadata, std::move(metadata_updater), std::move(metadata_refresher))};
}

DirectoryIterator& DirectoryIterator::operator++() {
  ++base_;
  return *this;
}

DirectoryIterator& DirectoryIterator::operator--() {
  --base_;
  return *this;
}

DirectoryIterator DirectoryIterator::operator++(int) {
  DirectoryIterator tmp(*this);
  ++(*this);
  return tmp;
}

DirectoryIterator DirectoryIterator::operator--(int) {
  DirectoryIterator tmp(*this);
  --(*this);
  return tmp;
}
