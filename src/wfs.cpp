/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs.h"

#include <array>
#include <bit>
#include <filesystem>

#include "area.h"
#include "blocks_device.h"
#include "directory.h"
#include "structs.h"

Wfs::Wfs(std::shared_ptr<BlocksDevice> device, std::shared_ptr<MetadataBlock> root_block)
    : device_(std::move(device)), root_block_(std::move(root_block)) {}

Wfs::~Wfs() {
  Flush();
}

std::shared_ptr<WfsItem> Wfs::GetObject(const std::string& filename) {
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

std::shared_ptr<File> Wfs::GetFile(const std::string& filename) {
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

std::shared_ptr<Directory> Wfs::GetDirectory(const std::string& filename) {
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

void Wfs::Flush() {
  device_->FlushAll();
}

std::shared_ptr<Area> Wfs::GetRootArea() {
  return std::make_shared<Area>(shared_from_this(), root_block_);
}

std::expected<std::shared_ptr<Directory>, WfsError> Wfs::GetRootDirectory() {
  return GetRootArea()->GetRootDirectory("", {root_block_, root_block_->to_offset(&header()->root_quota_attributes)});
}

// static
std::expected<std::shared_ptr<Wfs>, WfsError> Wfs::Load(std::shared_ptr<BlocksDevice> device) {
  auto block = MetadataBlock::LoadBlock(device, 0, Block::BlockSize::Basic, 0);
  if (!block.has_value()) {
    block = MetadataBlock::LoadBlock(device, 0, Block::BlockSize::Regular, 0);
    if (!block.has_value())
      return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }
  auto* header = (*block)->get_object<WfsHeader>(header_offset());
  if (header->version.value() != WFS_VERSION)
    return std::unexpected(WfsError::kInvalidWfsVersion);
  return std::make_shared<Wfs>(std::move(device), std::move(*block));
}
