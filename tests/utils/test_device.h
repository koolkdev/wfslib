/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <wfslib/device.h>

class TestDevice : public Device {
 public:
  TestDevice(uint32_t sectors_count)
      : sectors_count_(sectors_count), log2_sector_size_(9) {}
  ~TestDevice() override = default;

  void ReadSectors(const std::span<std::byte>& /*data*/,
                   uint32_t /*sector_address*/,
                   uint32_t /*sectors_count*/) override {
    return;
  }
  void WriteSectors(const std::span<std::byte>& /*data*/,
                    uint32_t /*sector_address*/,
                    uint32_t /*sectors_count*/) override {
    return;
  }
  uint32_t SectorsCount() const override { return sectors_count_; }
  uint32_t Log2SectorSize() const override { return log2_sector_size_; }
  bool IsReadOnly() const override { return false; }
  void SetSectorsCount(uint32_t sectors_count) override { sectors_count_ = sectors_count; }
  void SetLog2SectorSize(uint32_t log2_sector_size) override { log2_sector_size_ = log2_sector_size; }

 private:
  uint32_t sectors_count_;
  uint32_t log2_sector_size_;
};
