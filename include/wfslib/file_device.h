/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include "device.h"

class FileDevice : public Device {
 public:
  FileDevice(const std::string& path, uint32_t log2_sector_size = 9 /* 512 */, bool read_only = true);
  std::vector<uint8_t> ReadSectors(uint32_t sector_address, uint32_t sectors_count);
  void WriteSectors(const std::vector<uint8_t>& data, uint32_t sector_address, uint32_t sectors_count);
  uint32_t GetSectorsCount() { return sectors_count_; }
  uint32_t GetLog2SectorSize() { return log2_sector_size_; }

  void SetSectorsCount(uint32_t sectors_count) { sectors_count_ = sectors_count; }
  void SetLog2SectorSize(uint32_t log2_sector_size) { log2_sector_size_ = log2_sector_size; }

 private:
  std::unique_ptr<std::iostream> file_;
  std::mutex io_lock_;

  uint32_t log2_sector_size_;
  uint32_t sectors_count_;
  bool read_only_;
};
