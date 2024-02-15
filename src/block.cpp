/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "block.h"

#include "blocks_device.h"
#include "device.h"
#include "utils.h"

Block::Block(const std::shared_ptr<BlocksDevice>& device,
             uint32_t block_number,
             Block::BlockSize size_category,
             uint32_t data_size,
             uint32_t iv,
             bool encrypted)
    : device_(device),
      block_number_(block_number),
      size_category_(size_category),
      data_size_(data_size),
      iv_(iv),
      encrypted_(encrypted),
      data_{GetAlignedSize(data_size_), std::byte{0}} {}

Block::~Block() {
  device_->RemoveFromCache(block_number_);
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

bool Block::Fetch(bool check_hash) {
  return device_->ReadBlock(block_number_, 1 << (size_category_ - BlockSize::Basic), data_, as_const(this)->Hash(), iv_,
                            encrypted_, check_hash);
}

void Block::Flush() {
  if (!dirty_)
    return;
  device_->WriteBlock(block_number_, 1 << (size_category_ - BlockSize::Basic), data_, Hash(), iv_, encrypted_,
                      /*recalculate_hash=*/true);
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
