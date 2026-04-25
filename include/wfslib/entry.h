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
  class MetadataHandle;
  using MetadataHandlePtr = std::shared_ptr<MetadataHandle>;

  Entry(std::string name, MetadataHandlePtr metadata, std::shared_ptr<DirectoryMap> directory_map);
  virtual ~Entry();

  std::string_view name() const { return name_; }
  bool is_directory() const { return !metadata()->is_link() && metadata()->is_directory(); }
  bool is_file() const { return !metadata()->is_link() && !metadata()->is_directory(); }
  bool is_link() const { return metadata()->is_link(); }
  bool is_quota() const { return metadata()->is_directory() && metadata()->is_quota(); }

  uint32_t owner() const;
  uint32_t group() const;
  uint32_t mode() const;
  uint32_t creation_time() const;
  uint32_t modification_time() const;

  static std::expected<std::shared_ptr<Entry>, WfsError> Load(std::shared_ptr<QuotaArea> quota,
                                                              std::string name,
                                                              MetadataHandlePtr metadata,
                                                              std::shared_ptr<DirectoryMap> directory_map);
  static MetadataHandlePtr CreateMetadataHandle(MetadataRef metadata);

 protected:
  // TODO: Metadata copy as it can change?
  EntryMetadata* mutable_metadata();
  const EntryMetadata* metadata() const;
  const std::shared_ptr<Block>& metadata_block() const;

  std::string name_;
  MetadataHandlePtr metadata_;
  std::shared_ptr<DirectoryMap> directory_map_;
};

class Entry::MetadataHandle {
 public:
  explicit MetadataHandle(MetadataRef metadata);

  const EntryMetadata* get() const;
  EntryMetadata* get_mutable() const;
  const std::shared_ptr<Block>& block() const;

 private:
  friend class DirectoryEntryCache;

  void Update(MetadataRef metadata);
  void Invalidate();
  const MetadataRef& metadata_ref() const;

  std::optional<MetadataRef> metadata_;
};
