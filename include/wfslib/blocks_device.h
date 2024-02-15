/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

class Block;

class Device;
class DeviceEncryption;

class BlocksDevice {
 public:
  BlocksDevice(const std::shared_ptr<Device>& device, const std::span<std::byte>& key);
  virtual ~BlocksDevice() = default;

  virtual void WriteBlock(uint32_t block_number,
                          uint32_t size_in_blocks,
                          const std::span<std::byte>& data,
                          const std::span<std::byte>& hash,
                          uint32_t iv,
                          bool encrypt,
                          bool recalculate_hash);
  virtual bool ReadBlock(uint32_t block_number,
                         uint32_t size_in_blocks,
                         const std::span<std::byte>& data,
                         const std::span<const std::byte>& hash,
                         uint32_t iv,
                         bool encrypt,
                         bool check_hash);

  const Device* device() const { return device_.get(); }

  std::shared_ptr<Block> GetFromCache(uint32_t block_number);
  void AddToCache(uint32_t block_number, std::weak_ptr<Block> block);
  void RemoveFromCache(uint32_t block_number);
  void FlushAll();

 private:
  uint32_t ToDeviceSector(uint32_t block_number) const;

  std::shared_ptr<Device> device_;
  std::unique_ptr<DeviceEncryption> device_encryption_;
  std::unordered_map<uint32_t, std::weak_ptr<Block>> blocks_cache_;
};
