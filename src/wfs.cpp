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

Wfs::Wfs(std::shared_ptr<Device> device, std::optional<std::vector<std::byte>> key)
    : Wfs(std::make_shared<BlocksDevice>(std::move(device), std::move(key))) {}

Wfs::Wfs(std::shared_ptr<BlocksDevice> device) : device_(std::move(device)) {
  // Read first area
  auto area = Area::LoadRootArea(device_);
  if (!area.has_value())
    throw std::runtime_error("Failed to load first block (bad key?)");
  if (as_const(area->get())->wfs_header()->version.value() != WFS_VERSION)
    throw std::runtime_error("Unexpected WFS version (bad key?)");
  root_area_ = *area;
}

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
  auto current_directory = root_area_->GetRootDirectory();
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
