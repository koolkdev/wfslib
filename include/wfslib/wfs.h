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
#include <span>
#include <string>
#include <vector>

#include "errors.h"
#include "metadata_block.h"
#include "structs.h"

class Device;
class FileDevice;
class BlocksDevice;
class Area;
class WfsItem;
class File;
class Directory;
class BlocksDevice;

class Wfs : public std::enable_shared_from_this<Wfs> {  // -> WfsDevice
 public:
  Wfs(std::shared_ptr<BlocksDevice> device, std::shared_ptr<MetadataBlock> root_block);
  ~Wfs();

  const std::shared_ptr<BlocksDevice>& GetDevice() { return device_; }

  std::shared_ptr<WfsItem> GetObject(const std::string& filename);
  std::shared_ptr<File> GetFile(const std::string& filename);
  std::shared_ptr<Directory> GetDirectory(const std::string& filename);

  std::shared_ptr<Area> GetRootArea();
  std::expected<std::shared_ptr<Directory>, WfsError> GetRootDirectory();

  void Flush();

  WfsHeader* mutable_header() { return root_block_->get_mutable_object<WfsHeader>(header_offset()); }
  const WfsHeader* header() const { return root_block_->get_object<WfsHeader>(header_offset()); }

  static std::expected<std::shared_ptr<Wfs>, WfsError> Load(std::shared_ptr<BlocksDevice> device);
  // Create

 private:
  static constexpr uint16_t header_offset() { return sizeof(MetadataBlockHeader); }

  std::shared_ptr<BlocksDevice> device_;
  std::shared_ptr<MetadataBlock> root_block_;
};
