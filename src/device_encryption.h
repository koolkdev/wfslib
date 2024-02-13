/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cryptopp/sha.h>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

class Device;
struct WfsBlockIV;
class Block;

class DeviceEncryption {
 public:
  DeviceEncryption(const std::shared_ptr<Device>& device, const std::span<std::byte>& key);

  void WriteBlock(uint32_t sector_address, const std::span<std::byte>& data, uint32_t iv, bool encrypt);
  void ReadBlock(uint32_t sector_address, const std::span<std::byte>& data, uint32_t iv, bool encrypt) const;

  void CalculateHash(const std::span<const std::byte>& data, const std::span<std::byte>& hash) const;
  bool CheckHash(const std::span<const std::byte>& data, const std::span<const std::byte>& hash) const;

  std::shared_ptr<const Device> device() { return device_; }

  static constexpr size_t DIGEST_SIZE = CryptoPP::SHA1::DIGESTSIZE;

  // TODO: Move to another class
  std::shared_ptr<Block> GetFromCache(uint32_t block_number);
  void AddToCache(uint32_t block_number, std::weak_ptr<Block> block);
  void RemoveFromCache(uint32_t block_number);
  void FlushAll();

 private:
  void HashData(const std::list<std::span<const std::byte>>& data, const std::span<std::byte>& hash) const;
  WfsBlockIV GetIV(uint32_t sectors_count, uint32_t iv) const;

  std::shared_ptr<Device> device_;

  const std::vector<std::byte> key_;

  std::unordered_map<uint32_t, std::weak_ptr<Block>> blocks_cache_;
};
