/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "block.h"

#include <boost/format.hpp>
#include "device.h"
#include "device_encryption.h"
#include "utils.h"

Block::BadHash::BadHash(uint32_t block_number)
    : block_number_(block_number), msg_((boost::format("Bad hash for block 0x%08X") % block_number_).str()) {}
char const* Block::BadHash::what() const noexcept {
  return msg_.c_str();
}

Block::Block(const std::shared_ptr<DeviceEncryption>& device,
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

void Block::Fetch(bool check_hash) {
  device_->ReadBlock(ToDeviceSector(block_number_), data_, iv_, encrypted_);
  if (check_hash && !device_->CheckHash(data_, as_const(this)->Hash()))
    throw Block::BadHash(block_number_);
}

void Block::Flush() {
  if (!dirty_)
    return;
  device_->CalculateHash(data_, Hash());
  device_->WriteBlock(ToDeviceSector(block_number_), data_, iv_, encrypted_);
  dirty_ = false;
}

uint32_t Block::ToDeviceSector(uint32_t block_number) const {
  return block_number << (BlockSize::Basic - device_->device()->Log2SectorSize());
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
