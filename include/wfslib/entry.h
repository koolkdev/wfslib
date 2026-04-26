/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "block.h"
#include "structs.h"

class QuotaArea;
class DirectoryMap;
class DirectoryEntryCache;

class Entry {
 public:
  using MetadataRef = Block::DataRef<EntryMetadata>;
  class EntryHandle;
  using EntryHandlePtr = std::shared_ptr<EntryHandle>;

  explicit Entry(EntryHandlePtr handle);
  virtual ~Entry();

  std::string name() const;
  bool is_directory() const { return !metadata()->is_link() && metadata()->is_directory(); }
  bool is_file() const { return !metadata()->is_link() && !metadata()->is_directory(); }
  bool is_link() const { return metadata()->is_link(); }
  bool is_quota() const { return metadata()->is_directory() && metadata()->is_quota(); }

  uint32_t owner() const;
  uint32_t group() const;
  uint32_t mode() const;
  uint32_t creation_time() const;
  uint32_t modification_time() const;

  static std::expected<std::shared_ptr<Entry>, WfsError> Load(std::shared_ptr<QuotaArea> quota, EntryHandlePtr handle);
  static EntryHandlePtr CreateEntryHandle(std::shared_ptr<DirectoryMap> directory_map,
                                          std::string key,
                                          MetadataRef metadata);
  static EntryHandlePtr CreateSyntheticEntryHandle(std::string name, MetadataRef metadata);

 protected:
  EntryMetadata* mutable_metadata();
  const EntryMetadata* metadata() const;
  const std::shared_ptr<Block>& metadata_block() const;

  EntryHandlePtr handle_;
};

class Entry::EntryHandle {
 public:
  EntryHandle(std::shared_ptr<DirectoryMap> directory_map, std::string key, MetadataRef metadata);

  std::string_view key() const;
  const std::shared_ptr<DirectoryMap>& directory_map() const;
  const EntryMetadata* get() const;
  EntryMetadata* get_mutable() const;
  const std::shared_ptr<Block>& block() const;

 private:
  friend class DirectoryEntryCache;

  void Update(std::string key, MetadataRef metadata);
  void Invalidate();
  const MetadataRef& metadata_ref() const;

  std::shared_ptr<DirectoryMap> directory_map_;
  std::string key_;
  std::optional<MetadataRef> metadata_;
};
