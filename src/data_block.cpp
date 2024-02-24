/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "data_block.h"

#include "blocks_device.h"
#include "device_encryption.h"
#include "metadata_block.h"
#include "structs.h"

DataBlock::DataBlock(const std::shared_ptr<BlocksDevice>& device,
                     uint32_t block_number,
                     Block::BlockSize size_category,
                     uint32_t data_size,
                     uint32_t iv,
                     const DataBlockHash& data_hash,
                     bool encrypted)
    : Block(device, block_number, size_category, data_size, iv, encrypted), data_hash_(data_hash) {}

DataBlock::~DataBlock() {
  Flush();
}

std::expected<std::shared_ptr<DataBlock>, WfsError> DataBlock::LoadBlock(const std::shared_ptr<BlocksDevice>& device,
                                                                         uint32_t block_number,
                                                                         Block::BlockSize size_category,
                                                                         uint32_t data_size,
                                                                         uint32_t iv,
                                                                         const DataBlockHash& data_hash,
                                                                         bool encrypted) {
  auto cached_block = device->GetFromCache(block_number);
  if (cached_block) {
    assert(cached_block->BlockNumber() == block_number);
    assert(cached_block->log2_size() == size_category);
    assert(cached_block->size() == data_size);
    assert(cached_block->encrypted() == encrypted);
    auto block = std::dynamic_pointer_cast<DataBlock>(cached_block);
    assert(block);
    return block;
  }
  auto block = std::make_shared<DataBlock>(device, block_number, size_category, data_size, iv, data_hash, encrypted);
  device->AddToCache(block_number, block);
  if (data_size) {
    // Fetch block only if have data
    if (!block->Fetch())
      return std::unexpected(WfsError::kBlockBadHash);
  }
  return block;
}

std::span<std::byte> DataBlock::MutableHash() {
  return {hash_metadata_block()->mutable_data().data() + hash_offset(), DeviceEncryption::DIGEST_SIZE};
}

std::span<const std::byte> DataBlock::Hash() const {
  return {hash_metadata_block()->data().data() + hash_offset(), DeviceEncryption::DIGEST_SIZE};
}
