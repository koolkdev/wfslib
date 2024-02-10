/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "metadata_block.h"

#include "device_encryption.h"
#include "structs.h"

MetadataBlock::MetadataBlock(const std::shared_ptr<DeviceEncryption>& device,
                             uint32_t block_number,
                             Block::BlockSize size_category,
                             uint32_t iv)
    : Block(device, block_number, size_category, iv, true, {1ULL << size_category, std::byte{0}}) {}

std::shared_ptr<MetadataBlock> MetadataBlock::LoadBlock(const std::shared_ptr<DeviceEncryption>& device,
                                                        uint32_t block_number,
                                                        Block::BlockSize size_category,
                                                        uint32_t iv,
                                                        bool check_hash) {
  auto block = std::make_shared<MetadataBlock>(device, block_number, size_category, iv);
  block->Fetch(check_hash);
  return block;
}

std::shared_ptr<const MetadataBlock> MetadataBlock::LoadConstBlock(const std::shared_ptr<DeviceEncryption>& device,
                                                                   uint32_t block_number,
                                                                   Block::BlockSize size_category,
                                                                   uint32_t iv,
                                                                   bool check_hash) {
  return LoadBlock(device, block_number, size_category, iv, check_hash);
}

MetadataBlockHeader* MetadataBlock::Header() {
  return reinterpret_cast<MetadataBlockHeader*>(data_.data());
}

const MetadataBlockHeader* MetadataBlock::Header() const {
  return reinterpret_cast<const MetadataBlockHeader*>(data_.data());
}

std::span<std::byte> MetadataBlock::Hash() {
  return {Data().data() + offsetof(MetadataBlockHeader, hash), device_->DIGEST_SIZE};
}

std::span<const std::byte> MetadataBlock::Hash() const {
  return {Data().data() + offsetof(MetadataBlockHeader, hash), device_->DIGEST_SIZE};
}
