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

#include "block.h"
#include "errors.h"
#include "structs.h"

class BlocksDevice;
class Area;
class WfsItem;
class File;
class Directory;
class BlocksDevice;

class WfsDevice : public std::enable_shared_from_this<WfsDevice> {
 public:
  WfsDevice(std::shared_ptr<BlocksDevice> device, std::shared_ptr<Block> root_block);
  ~WfsDevice();

  BlocksDevice* device() { return device_.get(); }

  std::shared_ptr<WfsItem> GetObject(const std::string& filename);
  std::shared_ptr<File> GetFile(const std::string& filename);
  std::shared_ptr<Directory> GetDirectory(const std::string& filename);

  std::shared_ptr<Area> GetRootArea();
  std::expected<std::shared_ptr<Directory>, WfsError> GetRootDirectory();

  void Flush();

  std::expected<std::shared_ptr<Area>, WfsError> GetTransactionsArea(bool backup_area = false);

  std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(const Area* area,
                                                                    uint32_t device_block_number,
                                                                    Block::BlockSize size,
                                                                    bool new_block = false) const;
  std::expected<std::shared_ptr<Block>, WfsError> LoadDataBlock(const Area* area,
                                                                uint32_t device_block_number,
                                                                Block::BlockSize size,
                                                                uint32_t data_size,
                                                                Block::HashRef data_hash,
                                                                bool encrypted,
                                                                bool new_block = false) const;

  uint32_t CalcIV(const Area* area, uint32_t device_block_number) const;

  static std::expected<std::shared_ptr<WfsDevice>, WfsError> Open(std::shared_ptr<BlocksDevice> device);
  // Create

 private:
  friend class Area;

  static constexpr uint16_t header_offset() { return sizeof(MetadataBlockHeader); }

  auto* mutable_header() { return root_block_->get_mutable_object<WfsDeviceHeader>(header_offset()); }
  const auto* header() const { return root_block_->get_object<WfsDeviceHeader>(header_offset()); }

  std::shared_ptr<BlocksDevice> device_;
  std::shared_ptr<Block> root_block_;
};
