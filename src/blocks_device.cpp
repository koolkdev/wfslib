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

BlocksDevice::BlocksDevice(std::shared_ptr<Device> device, std::optional<std::span<std::byte>> key)
    : device_(std::move(device)),
      device_encryption_(key ? std::make_unique<DeviceEncryption>(device_, std::move(*key)) : nullptr) {}

void BlocksDevice::WriteBlock(uint32_t block_number,
                              uint32_t /*size_in_blocks*/,
                              const std::span<std::byte>& data,
                              const std::span<std::byte>& hash,
                              uint32_t iv,
                              bool encrypt,
                              bool recalculate_hash) {
  assert(data.size() % device_->SectorSize() == 0);
  auto const sector_address = ToDeviceSector(block_number);
  auto const sectors_count = static_cast<uint32_t>(data.size() / device_->SectorSize());

  if (recalculate_hash)
    DeviceEncryption::CalculateHash(data, hash);

  if (encrypt && device_encryption_) {
    std::vector<std::byte> enc_data(data.begin(), data.end());
    device_encryption_->EncryptBlock(enc_data, iv);
    device_->WriteSectors(enc_data, sector_address, sectors_count);
  } else {
    device_->WriteSectors(data, sector_address, sectors_count);
  }
}

bool BlocksDevice::ReadBlock(uint32_t block_number,
                             uint32_t /*size_in_blocks*/,
                             const std::span<std::byte>& data,
                             const std::span<const std::byte>& hash,
                             uint32_t iv,
                             bool encrypt,
                             bool check_hash) {
  assert(data.size() % device_->SectorSize() == 0);
  auto const sector_address = ToDeviceSector(block_number);
  auto const sectors_count = static_cast<uint32_t>(data.size() / device_->SectorSize());
  device_->ReadSectors(data, sector_address, sectors_count);

  if (encrypt && device_encryption_)
    device_encryption_->DecryptBlock(data, iv);
  return !check_hash || DeviceEncryption::CheckHash(data, hash);
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
