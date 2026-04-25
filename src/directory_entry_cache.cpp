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

  auto handle = HandleFor(val.name, val.metadata);
  auto entry = Entry::Load(std::move(quota), handle, directory_map.shared_from_this());
  if (entry.has_value())
    entries_[val.name] = *entry;
  return entry;
}

void DirectoryEntryCache::MetadataUpdated(std::string_view name, Block::DataRef<EntryMetadata> metadata) {
  auto it = handles_.find(std::string{name});
  if (it == handles_.end())
    return;
  if (auto handle = it->second.lock()) {
    auto real_name = metadata->GetCaseSensitiveName(name);
    handle->Update(std::move(real_name), std::move(metadata));
  } else {
    handles_.erase(it);
  }
}

void DirectoryEntryCache::EntryRemoved(std::string_view name) {
  auto key = std::string{name};
  entries_.erase(key);

  auto it = handles_.find(key);
  if (it == handles_.end())
    return;
  if (auto handle = it->second.lock())
    handle->Invalidate();
  handles_.erase(it);
}

void DirectoryEntryCache::RefreshMetadataRefs(const DirectoryMap& directory_map) {
  std::vector<std::pair<std::string, Entry::EntryHandlePtr>> live_handles;
  for (auto it = handles_.begin(); it != handles_.end();) {
    if (auto handle = it->second.lock()) {
      live_handles.emplace_back(it->first, std::move(handle));
      ++it;
    } else {
      it = handles_.erase(it);
    }
  }

  for (auto& [name, handle] : live_handles) {
    auto it = directory_map.find(name);
    if (it.is_end()) {
      handle->Invalidate();
    } else {
      auto metadata = (*it).metadata;
      auto real_name = metadata->GetCaseSensitiveName(name);
      handle->Update(std::move(real_name), std::move(metadata));
    }
  }
}

Entry::EntryHandlePtr DirectoryEntryCache::HandleFor(std::string_view key, Block::DataRef<EntryMetadata> metadata) {
  auto map_key = std::string{key};
  if (auto it = handles_.find(map_key); it != handles_.end()) {
    if (auto handle = it->second.lock()) {
      auto name = metadata->GetCaseSensitiveName(key);
      handle->Update(std::move(name), std::move(metadata));
      return handle;
    }
    handles_.erase(it);
  }

  auto name = metadata->GetCaseSensitiveName(key);
  auto handle = Entry::CreateEntryHandle(std::move(name), std::move(metadata));
  handles_.emplace(std::move(map_key), handle);
  return handle;
}
