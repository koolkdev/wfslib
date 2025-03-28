/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <cassert>

#include "block.h"
#include "test_blocks_device.h"
#include "test_device.h"

TestBlocksDevice::TestBlocksDevice(uint32_t blocks_count)
    : BlocksDevice(std::make_shared<TestDevice>(blocks_count << (log2_size(BlockSize::Logical) - 9))) {}
TestBlocksDevice::~TestBlocksDevice() = default;

void TestBlocksDevice::WriteBlock(uint32_t block_number,
                                  uint32_t /*size_in_blocks*/,
                                  const std::span<std::byte>& data,
                                  const std::span<std::byte>& /*hash*/,
                                  uint32_t /*iv*/,
                                  bool /*encrypt*/,
                                  bool /*recalculate_hash*/) {
  // if (recalculate_hash)
  //   DeviceEncryption::CalculateHash(data, hash);
  blocks_[block_number] = {data.begin(), data.end()};
}

bool TestBlocksDevice::ReadBlock(uint32_t block_number,
                                 uint32_t /*size_in_blocks*/,
                                 const std::span<std::byte>& data,
                                 const std::span<const std::byte>& /*hash*/,
                                 uint32_t /*iv*/,
                                 bool /*encrypt*/,
                                 bool /*check_hash*/) {
  auto it = blocks_.find(block_number);
  if (it != blocks_.end()) {
    assert(data.size() == it->second.size());
    std::memcpy(data.data(), it->second.data(), data.size());
  } else {
    std::ranges::fill(data, std::byte{0});
  }
  // return !check_hash || DeviceEncryption::CheckHash(data, hash);
  return true;
}
