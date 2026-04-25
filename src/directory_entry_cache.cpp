/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_entry_cache.h"

#include <utility>
#include <vector>

#include "directory_map.h"
#include "quota_area.h"

std::expected<std::shared_ptr<Entry>, WfsError> DirectoryEntryCache::Load(DirectoryMap& directory_map,
                                                                          DirectoryMapIterator it,
                                                                          std::shared_ptr<QuotaArea> quota) {
  if (it.is_end())
    return std::unexpected(WfsError::kEntryNotFound);

  auto val = *it;
  if (auto entry_it = entries_.find(val.name); entry_it != entries_.end()) {
    if (auto entry = entry_it->second.lock())
      return entry;
    entries_.erase(entry_it);
  }

  auto metadata = HandleFor(val.name, val.metadata);
  auto name = metadata->get()->GetCaseSensitiveName(val.name);
  auto entry = Entry::Load(std::move(quota), name, metadata, directory_map.shared_from_this());
  if (entry.has_value())
    entries_[val.name] = *entry;
  return entry;
}

void DirectoryEntryCache::MetadataUpdated(std::string_view name, Block::DataRef<EntryMetadata> metadata) {
  auto it = metadata_handles_.find(std::string{name});
  if (it == metadata_handles_.end())
    return;
  if (auto handle = it->second.lock()) {
    handle->Update(std::move(metadata));
  } else {
    metadata_handles_.erase(it);
  }
}

void DirectoryEntryCache::EntryRemoved(std::string_view name) {
  auto key = std::string{name};
  entries_.erase(key);

  auto it = metadata_handles_.find(key);
  if (it == metadata_handles_.end())
    return;
  if (auto handle = it->second.lock())
    handle->Invalidate();
  metadata_handles_.erase(it);
}

void DirectoryEntryCache::RefreshMetadataRefs(const DirectoryMap& directory_map) {
  std::vector<std::pair<std::string, Entry::MetadataHandlePtr>> live_handles;
  for (auto it = metadata_handles_.begin(); it != metadata_handles_.end();) {
    if (auto handle = it->second.lock()) {
      live_handles.emplace_back(it->first, std::move(handle));
      ++it;
    } else {
      it = metadata_handles_.erase(it);
    }
  }

  for (auto& [name, handle] : live_handles) {
    auto it = directory_map.find(name);
    if (it.is_end()) {
      handle->Invalidate();
    } else {
      handle->Update((*it).metadata);
    }
  }
}

Entry::MetadataHandlePtr DirectoryEntryCache::HandleFor(std::string_view name,
                                                        Block::DataRef<EntryMetadata> metadata) {
  auto key = std::string{name};
  if (auto it = metadata_handles_.find(key); it != metadata_handles_.end()) {
    if (auto handle = it->second.lock()) {
      handle->Update(std::move(metadata));
      return handle;
    }
    metadata_handles_.erase(it);
  }

  auto handle = Entry::CreateMetadataHandle(std::move(metadata));
  metadata_handles_.emplace(std::move(key), handle);
  return handle;
}
