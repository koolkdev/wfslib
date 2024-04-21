/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs_device.h"

#include <array>
#include <bit>
#include <filesystem>

#include "area.h"
#include "blocks_device.h"
#include "device.h"
#include "directory.h"
#include "structs.h"

WfsDevice::WfsDevice(std::shared_ptr<BlocksDevice> device, std::shared_ptr<Block> root_block)
    : device_(std::move(device)), root_block_(std::move(root_block)) {}

WfsDevice::~WfsDevice() {
  Flush();
}

std::shared_ptr<WfsItem> WfsDevice::GetObject(const std::string& filename) {
  if (filename == "/")
    return GetDirectory("/");
  std::filesystem::path path(filename);
  auto dir = GetDirectory(path.parent_path().string());
  if (!dir)
    return nullptr;
  auto obj = dir->GetObject(path.filename().string());
  if (!obj.has_value()) {
    if (obj.error() == WfsError::kItemNotFound)
      return nullptr;
    else
      throw WfsException(obj.error());
  }
  return *obj;
}

std::shared_ptr<File> WfsDevice::GetFile(const std::string& filename) {
  std::filesystem::path path(filename);
  auto dir = GetDirectory(path.parent_path().string());
  if (!dir)
    return nullptr;
  auto file = dir->GetFile(path.filename().string());
  if (!file.has_value()) {
    if (file.error() == WfsError::kItemNotFound)
      return nullptr;
    else
      throw WfsException(file.error());
  }
  return *file;
}

std::shared_ptr<Directory> WfsDevice::GetDirectory(const std::string& filename) {
  std::filesystem::path path(filename);
  auto current_directory = GetRootDirectory();
  if (!current_directory.has_value()) {
    if (current_directory.error() == WfsError::kItemNotFound)
      return nullptr;
    else
      throw WfsException(current_directory.error());
  }

  for (const auto& part : path) {
    // the first part is "/"
    if (part == "/")
      continue;
    current_directory = (*current_directory)->GetDirectory(part.string());
    if (!current_directory.has_value()) {
      if (current_directory.error() == WfsError::kItemNotFound)
        return nullptr;
      else
        throw WfsException(current_directory.error());
    }
  }
  return *current_directory;
}

void WfsDevice::Flush() {
  device_->FlushAll();
}

std::shared_ptr<Area> WfsDevice::GetRootArea() {
  return std::make_shared<Area>(shared_from_this(), root_block_);
}

std::expected<std::shared_ptr<Directory>, WfsError> WfsDevice::GetRootDirectory() {
  return GetRootArea()->LoadRootDirectory("", {root_block_, root_block_->to_offset(&header()->root_quota_attributes)});
}

// static
std::expected<std::shared_ptr<WfsDevice>, WfsError> WfsDevice::Open(std::shared_ptr<BlocksDevice> device) {
  auto block = Block::LoadMetadataBlock(device, /*dvice_block_number=*/0, Block::BlockSize::Basic, /*iv=*/0);
  if (!block.has_value()) {
    block = Block::LoadMetadataBlock(device, /*dvice_block_number=*/0, Block::BlockSize::Regular, /*iv=*/0);
    if (!block.has_value())
      return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }
  auto* header = (*block)->get_object<WfsDeviceHeader>(header_offset());
  if (header->version.value() != WFS_VERSION)
    return std::unexpected(WfsError::kInvalidWfsVersion);
  return std::make_shared<WfsDevice>(std::move(device), std::move(*block));
}

std::expected<std::shared_ptr<Block>, WfsError> WfsDevice::LoadMetadataBlock(const Area* area,
                                                                             uint32_t device_block_number,
                                                                             Block::BlockSize size,
                                                                             bool new_block) const {
  return Block::LoadMetadataBlock(device_, device_block_number, size, CalcIV(area, device_block_number), !new_block);
}

std::expected<std::shared_ptr<Block>, WfsError> WfsDevice::LoadDataBlock(const Area* area,
                                                                         uint32_t device_block_number,
                                                                         Block::BlockSize size,
                                                                         uint32_t data_size,
                                                                         Block::HashRef data_hash,
                                                                         bool encrypted,
                                                                         bool new_block) const {
  return Block::LoadDataBlock(device_, device_block_number, size, data_size, CalcIV(area, device_block_number),
                              std::move(data_hash), encrypted, !new_block);
}

uint32_t WfsDevice::CalcIV(const Area* area, uint32_t device_block_number) const {
  return (area->header()->iv.value() ^ header()->iv.value()) +
         ((device_block_number - area->device_block_number())
          << (Block::BlockSize::Basic - device_->device()->Log2SectorSize()));
}

std::expected<std::shared_ptr<Area>, WfsError> WfsDevice::GetTransactionsArea(bool backup_area) {
  auto root_area = GetRootArea();
  auto block =
      LoadMetadataBlock(root_area.get(), header()->transactions_area_block_number.value() + (backup_area ? 1 : 0),
                        Block::BlockSize::Basic);
  if (!block.has_value())
    return std::unexpected(WfsError::kTransactionsAreaCorrupted);
  return std::make_shared<Area>(shared_from_this(), *block);
}
