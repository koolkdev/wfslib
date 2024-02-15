/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

class Device;
class FileDevice;
class BlocksDevice;
class Area;
class WfsItem;
class File;
class Directory;
class BlocksDevice;

class Wfs {
 public:
  Wfs(const std::shared_ptr<Device>& device, const std::span<std::byte>& key);
  Wfs(const std::shared_ptr<BlocksDevice>& device);
  ~Wfs();

  const std::shared_ptr<BlocksDevice>& GetDevice() { return device_; }

  std::shared_ptr<WfsItem> GetObject(const std::string& filename);
  std::shared_ptr<File> GetFile(const std::string& filename);
  std::shared_ptr<Directory> GetDirectory(const std::string& filename);

  static void DetectDeviceSectorSizeAndCount(const std::shared_ptr<FileDevice>& device,
                                             const std::span<std::byte>& key);

  std::shared_ptr<Area> GetRootArea() { return root_area_; }

  void Flush();

 private:
  std::shared_ptr<BlocksDevice> device_;
  std::shared_ptr<Area> root_area_;

  static bool VerifyDeviceAndKey(const std::shared_ptr<FileDevice>& device, const std::span<std::byte>& key);
};
