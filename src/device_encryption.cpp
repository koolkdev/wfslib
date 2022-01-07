/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "device_encryption.h"

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include <array>
#include <boost/endian/buffers.hpp>
#include "device.h"

struct WfsBlockIV {
  boost::endian::big_uint32_buf_t iv[4];
};

DeviceEncryption::DeviceEncryption(const std::shared_ptr<Device>& device, const std::span<uint8_t>& key)
    : device_(device), key_(key.begin(), key.end()) {}

void DeviceEncryption::HashData(const std::span<uint8_t>& data, const std::span<uint8_t>& hash) {
  // Pad and hash
  CryptoPP::SHA1 sha;
  sha.Update(&*data.begin(), data.size());
  std::vector<uint8_t> pad(ToSectorSize(data.size()) - data.size(), 0);
  if (!pad.empty())
    sha.Update(&*pad.begin(), pad.size());
  sha.Final(&*hash.begin());
}

void DeviceEncryption::CalculateHash(const std::span<uint8_t>& data,
                                     const std::span<uint8_t>& hash,
                                     bool hash_in_block) {
  // Fill hash space with 0xFF
  if (hash_in_block)
    std::ranges::fill(hash, (uint8_t)0xFF);

  HashData(data, hash);
}

void DeviceEncryption::WriteBlock(uint32_t sector_address, const std::span<uint8_t>& data, uint32_t iv, bool encrypt) {
  // Pad with zeros
  std::vector<uint8_t> enc_data(data.begin(), data.end());
  enc_data.resize(ToSectorSize(data.size()), 0);

  uint32_t sectors_count = static_cast<uint32_t>(enc_data.size() / device_->GetSectorSize());

  if (encrypt) {
    // Encrypt
    auto _iv = GetIV(sectors_count, iv);
    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption encryptor(&*key_.begin(), key_.size(),
                                                            reinterpret_cast<uint8_t*>(&_iv));
    encryptor.ProcessData(&*enc_data.begin(), &*enc_data.begin(), enc_data.size());
  }

  // Write
  device_->WriteSectors(enc_data, sector_address, sectors_count);
}

std::vector<uint8_t> DeviceEncryption::ReadBlock(uint32_t sector_address, uint32_t length, uint32_t iv, bool encrypt) {
  uint8_t sectors_count = static_cast<uint8_t>(ToSectorSize(length) / device_->GetSectorSize());

  std::vector<uint8_t> data = device_->ReadSectors(sector_address, sectors_count);

  if (encrypt) {
    auto _iv = GetIV(sectors_count, iv);
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption decryptor(&*key_.begin(), key_.size(),
                                                            reinterpret_cast<uint8_t*>(&_iv));
    decryptor.ProcessData(&*data.begin(), &*data.begin(), data.size());
  }

  // data.resize(length);
  return data;
}

bool DeviceEncryption::CheckHash(const std::span<uint8_t>& data, const std::span<uint8_t>& hash, bool hash_in_block) {
  std::array<uint8_t, DIGEST_SIZE> placeholder_hash;
  placeholder_hash.fill(0xFF);
  if (hash_in_block)
    std::ranges::swap_ranges(placeholder_hash, hash);

  std::array<uint8_t, DIGEST_SIZE> calculated_hash;
  HashData(data, calculated_hash);

  if (hash_in_block)
    std::ranges::swap_ranges(placeholder_hash, hash);

  return std::ranges::equal(calculated_hash, hash);
}

WfsBlockIV DeviceEncryption::GetIV(uint32_t sectors_count, uint32_t iv) {
  WfsBlockIV aes_iv;
  aes_iv.iv[0] = sectors_count * device_->GetSectorSize();
  aes_iv.iv[1] = iv;
  aes_iv.iv[2] = device_->GetSectorsCount();
  aes_iv.iv[3] = device_->GetSectorSize();
  return aes_iv;
}

size_t DeviceEncryption::ToSectorSize(size_t size) {
  return size + (-static_cast<int32_t>(size)) % device_->GetSectorSize();
}
