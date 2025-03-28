/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "block.h"

#include "blocks_device.h"
#include "device.h"
#include "structs.h"
#include "utils.h"

Block::Block(std::shared_ptr<BlocksDevice> device,
             uint32_t physical_block_number,
             BlockSize block_size,
             BlockType block_type,
             uint32_t data_size,
             uint32_t iv,
             HashRef hash_ref,
             bool encrypted)
    : device_(std::move(device)),
      physical_block_number_(physical_block_number),
      block_size_(block_size),
      block_type_(block_type),
      data_size_(data_size),
      iv_(iv),
      encrypted_(encrypted),
      hash_ref_(std::move(hash_ref)),
      data_{GetAlignedSize(data_size_), std::byte{0}} {}

Block::Block(std::vector<std::byte> data)
    : physical_block_number_(0),
      block_size_(BlockSize::Physical),
      block_type_(BlockType::Single),
      data_size_(static_cast<uint32_t>(data.size())),
      iv_(0),
      encrypted_(false),
      detached_(true),
      hash_ref_{},
      data_(std::move(data)) {}

Block::~Block() {
  Flush();
  Detach();
}

void Block::Resize(uint32_t data_size) {
  assert(!device_->device()->IsReadOnly());
  // Ensure that block data is aligned to device sectors
  if (data_size_ == data_size)
    return;

  auto new_size = GetAlignedSize(data_size);
  if (new_size != data_.size()) {
    data_.resize(new_size, std::byte{0});
    dirty_ = true;
  }
}

void Block::Detach() {
  if (detached_)
    return;
  device_->RemoveFromCache(physical_block_number_);
  detached_ = true;
}

bool Block::Fetch(bool check_hash) {
  assert(!detached_);
  if (data_.size() == 0)
    return true;
  return device_->ReadBlock(physical_block_number_, 1 << (log2_size() - ::log2_size(BlockSize::Physical)), data_,
                            {hash(), DeviceEncryption::DIGEST_SIZE}, iv_, encrypted_, check_hash);
}

void Block::Flush() {
  if (detached_ || !dirty_)
    return;
  if (data_.size() > 0) {
    device_->WriteBlock(physical_block_number_, 1 << (log2_size() - ::log2_size(BlockSize::Physical)), data_,
                        {mutable_hash(), DeviceEncryption::DIGEST_SIZE}, iv_, encrypted_,
                        /*recalculate_hash=*/true);
  }
  dirty_ = false;
}

uint32_t Block::GetAlignedSize(uint32_t size) const {
  assert(size > 0 && size <= capacity());
  return static_cast<uint32_t>(div_ceil(size, device_->device()->SectorSize()) * device_->device()->SectorSize());
}

std::span<std::byte> Block::GetDataForWriting() {
  assert(!device_->device()->IsReadOnly());
  dirty_ = true;
  return {data_.data(), data_.data() + size()};
}

std::expected<std::shared_ptr<Block>, WfsError> Block::LoadDataBlock(std::shared_ptr<BlocksDevice> device,
                                                                     uint32_t physical_block_number,
                                                                     BlockSize block_size,
                                                                     BlockType block_type,
                                                                     uint32_t data_size,
                                                                     uint32_t iv,
                                                                     HashRef data_hash,
                                                                     bool encrypted,
                                                                     bool load_data,
                                                                     bool check_hash) {
  auto cached_block = device->GetFromCache(physical_block_number);
  if (cached_block) {
    assert(cached_block->physical_block_number() == physical_block_number);
    assert(cached_block->block_size() == block_size);
    assert(cached_block->block_type() == block_type);
    assert(cached_block->size() == data_size);
    assert(cached_block->encrypted() == encrypted);
    return cached_block;
  }
  auto block = std::make_shared<Block>(device, physical_block_number, block_size, block_type, data_size, iv,
                                       std::move(data_hash), encrypted);
  device->AddToCache(physical_block_number, block);
  if (load_data) {
    if (!block->Fetch(check_hash))
      return std::unexpected(WfsError::kBlockBadHash);
  }
  return block;
}

std::expected<std::shared_ptr<Block>, WfsError> Block::LoadMetadataBlock(std::shared_ptr<BlocksDevice> device,
                                                                         uint32_t block_number,
                                                                         BlockSize block_size,
                                                                         uint32_t iv,
                                                                         bool load_data,
                                                                         bool check_hash) {
  return LoadDataBlock(std::move(device), block_number, block_size, BlockType::Single, 1 << ::log2_size(block_size), iv,
                       {nullptr, offsetof(MetadataBlockHeader, hash)}, /*encrypted=*/true, load_data, check_hash);
}

std::shared_ptr<Block> Block::CreateDetached(std::vector<std::byte> data) {
  return std::make_shared<Block>(std::move(data));
}
