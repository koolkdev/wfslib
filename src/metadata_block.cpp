/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "metadata_block.h"

#include "blocks_device.h"
#include "device_encryption.h"
#include "structs.h"

MetadataBlock::MetadataBlock(const std::shared_ptr<BlocksDevice>& device,
                             uint32_t block_number,
                             Block::BlockSize size_category,
                             uint32_t iv)
    : Block(device, block_number, size_category, 1 << size_category, iv, true) {}

MetadataBlock::~MetadataBlock() {
  Flush();
}

std::expected<std::shared_ptr<MetadataBlock>, WfsError> MetadataBlock::LoadBlock(
    const std::shared_ptr<BlocksDevice>& device,
    uint32_t block_number,
    Block::BlockSize size_category,
    uint32_t iv,
    bool check_hash) {
  auto cached_block = device->GetFromCache(block_number);
  if (cached_block) {
    assert(cached_block->BlockNumber() == block_number);
    assert(cached_block->log2_size() == size_category);
    auto block = std::dynamic_pointer_cast<MetadataBlock>(cached_block);
    assert(block);
    return block;
  }
  auto block = std::make_shared<MetadataBlock>(device, block_number, size_category, iv);
  device->AddToCache(block_number, block);
  if (!block->Fetch(check_hash))
    return std::unexpected(WfsError::kBlockBadHash);
  return block;
}

MetadataBlockHeader* MetadataBlock::Header() {
  return reinterpret_cast<MetadataBlockHeader*>(data_.data());
}

const MetadataBlockHeader* MetadataBlock::Header() const {
  return reinterpret_cast<const MetadataBlockHeader*>(data_.data());
}

void MetadataBlock::Resize(uint32_t data_size) {
  std::ignore = data_size;
  // Can't resize metadata block
  assert(false);
}

std::span<std::byte> MetadataBlock::MutableHash() {
  return {mutable_data().data() + offsetof(MetadataBlockHeader, hash), DeviceEncryption::DIGEST_SIZE};
}

std::span<const std::byte> MetadataBlock::Hash() const {
  return {data().data() + offsetof(MetadataBlockHeader, hash), DeviceEncryption::DIGEST_SIZE};
}
