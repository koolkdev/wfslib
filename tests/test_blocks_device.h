/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <wfslib/blocks_device.h>
#include <map>
#include <vector>

class TestBlocksDevice : public BlocksDevice {
 public:
  TestBlocksDevice(uint32_t blocks_count = 10000);
  ~TestBlocksDevice() override;

  void WriteBlock(uint32_t block_number,
                  uint32_t size_in_blocks,
                  const std::span<std::byte>& data,
                  const std::span<std::byte>& hash,
                  uint32_t iv,
                  bool encrypt,
                  bool recalculate_hash) override;

  bool ReadBlock(uint32_t block_number,
                 uint32_t size_in_blocks,
                 const std::span<std::byte>& data,
                 const std::span<const std::byte>& hash,
                 uint32_t iv,
                 bool encrypt,
                 bool check_hash) override;

 public:
  std::map<uint32_t, std::vector<std::byte>> blocks_;
};
