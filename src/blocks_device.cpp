/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <cassert>

#include "block.h"
#include "blocks_device.h"
#include "device.h"
#include "device_encryption.h"

BlocksDevice::BlocksDevice(const std::shared_ptr<Device>& device, const std::span<std::byte>& key)
    : device_(device), device_encryption_(std::make_unique<DeviceEncryption>(device, key)) {}

void BlocksDevice::WriteBlock(uint32_t block_number,
                              uint32_t /*size_in_blocks*/,
                              const std::span<std::byte>& data,
                              const std::span<std::byte>& hash,
                              uint32_t iv,
                              bool encrypt,
                              bool recalculate_hash) {
  if (recalculate_hash)
    device_encryption_->CalculateHash(data, hash);
  device_encryption_->WriteBlock(ToDeviceSector(block_number), data, iv, encrypt);
}

bool BlocksDevice::ReadBlock(uint32_t block_number,
                             uint32_t /*size_in_blocks*/,
                             const std::span<std::byte>& data,
                             const std::span<const std::byte>& hash,
                             uint32_t iv,
                             bool encrypt,
                             bool check_hash) {
  device_encryption_->ReadBlock(ToDeviceSector(block_number), data, iv, encrypt);
  return !check_hash || device_encryption_->CheckHash(data, hash);
}

uint32_t BlocksDevice::ToDeviceSector(uint32_t block_number) const {
  return block_number << (Block::BlockSize::Basic - device()->Log2SectorSize());
}

std::shared_ptr<Block> BlocksDevice::GetFromCache(uint32_t block_number) {
  auto res = blocks_cache_.find(block_number);
  if (res == blocks_cache_.end())
    return nullptr;
  return std::shared_ptr<Block>(res->second);
}

void BlocksDevice::AddToCache(uint32_t block_number, std::weak_ptr<Block> block) {
  blocks_cache_[block_number] = block;
}

void BlocksDevice::RemoveFromCache(uint32_t block_number) {
  auto res = blocks_cache_.find(block_number);
  assert(res != blocks_cache_.end());
  blocks_cache_.erase(res);
}

void BlocksDevice::FlushAll() {
  for (auto& [block_number, block_weak] : blocks_cache_) {
    std::shared_ptr<Block>(block_weak)->Flush();
  }
}
