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

class Entry {
 public:
  using MetadataRef = Block::DataRef<EntryMetadata>;

  Entry(std::string name, MetadataRef block);
  virtual ~Entry();

  std::string_view name() const { return name_; }
  bool is_directory() const { return !metadata()->is_link() && metadata()->is_directory(); }
  bool is_file() const { return !metadata()->is_link() && !metadata()->is_directory(); }
  bool is_link() const { return metadata()->is_link(); }
  bool is_quota() const { return metadata()->is_directory() && metadata()->is_quota(); }

  static std::expected<std::shared_ptr<Entry>, WfsError> Load(std::shared_ptr<QuotaArea> quota,
                                                              std::string name,
                                                              MetadataRef metadata_ref);

 protected:
  // TODO: Metadata copy as it can change?
  EntryMetadata* mutable_metadata() { return metadata_.get_mutable(); }
  const EntryMetadata* metadata() const { return metadata_.get(); }
  const std::shared_ptr<Block>& metadata_block() const { return metadata_.block; }

  std::string name_;
  MetadataRef metadata_;
};
