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

#include "block.h"
#include "structs.h"

class QuotaArea;
class DirectoryMap;

class Entry {
 public:
  using MetadataRef = Block::DataRef<EntryMetadata>;
  struct MetadataHandle {
    explicit MetadataHandle(MetadataRef metadata) : metadata(std::move(metadata)) {}
    MetadataRef metadata;
  };
  using MetadataHandleRef = std::shared_ptr<MetadataHandle>;

  Entry(std::string name, MetadataHandleRef metadata);
  virtual ~Entry();

  static MetadataHandleRef CreateMetadataHandle(MetadataRef metadata);

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
                                                              MetadataHandleRef metadata,
                                                              std::shared_ptr<DirectoryMap> directory_map,
                                                              std::string directory_key);

 protected:
  // TODO: Metadata copy as it can change?
  EntryMetadata* mutable_metadata() { return metadata_->metadata.get_mutable(); }
  const EntryMetadata* metadata() const { return metadata_->metadata.get(); }
  const std::shared_ptr<Block>& metadata_block() const { return metadata_->metadata.block; }

  std::string name_;
  MetadataHandleRef metadata_;
};
