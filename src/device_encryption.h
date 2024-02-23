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
#include <vector>

class Device;
struct WfsBlockIV;

class DeviceEncryption {
 public:
  DeviceEncryption(std::shared_ptr<Device> device, std::vector<std::byte> key);

  void EncryptBlock(const std::span<std::byte>& data, uint32_t iv);
  void DecryptBlock(const std::span<std::byte>& data, uint32_t iv) const;

  static void CalculateHash(const std::span<const std::byte>& data, const std::span<std::byte>& hash);
  static bool CheckHash(const std::span<const std::byte>& data, const std::span<const std::byte>& hash);

  std::shared_ptr<const Device> device() { return device_; }

  static constexpr size_t DIGEST_SIZE = CryptoPP::SHA1::DIGESTSIZE;

 private:
  static void HashData(std::initializer_list<std::span<const std::byte>> data, const std::span<std::byte>& hash);
  WfsBlockIV GetIV(uint32_t sectors_count, uint32_t iv) const;

  std::shared_ptr<Device> device_;

  const std::vector<std::byte> key_;
};
