/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include "device.h"

class FileDevice : public Device {
 public:
  FileDevice(const std::filesystem::path& path,
             uint32_t log2_sector_size = 9 /* 512 */,
             uint32_t sectors_count = 0,
             bool read_only = true,
             bool open_create = false);
  void ReadSectors(const std::span<std::byte>& data, uint32_t sector_address, uint32_t sectors_count) override;
  void WriteSectors(const std::span<std::byte>& data, uint32_t sector_address, uint32_t sectors_count) override;
  uint32_t SectorsCount() const override { return sectors_count_; }
  uint32_t Log2SectorSize() const override { return log2_sector_size_; }
  bool IsReadOnly() const override { return read_only_; }

  void SetSectorsCount(uint32_t sectors_count) override { sectors_count_ = sectors_count; }
  void SetLog2SectorSize(uint32_t log2_sector_size) override { log2_sector_size_ = log2_sector_size; }

  uint64_t GetFileSize();

 private:
  std::unique_ptr<std::iostream> file_;
  std::mutex io_lock_;

  uint32_t log2_sector_size_;
  uint32_t sectors_count_;
  bool read_only_;
};
