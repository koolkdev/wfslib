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

Block::BadHash::BadHash(uint32_t block_number)
    : block_number_(block_number), msg_((boost::format("Bad hash for block 0x%08X") % block_number_).str()) {}
char const* Block::BadHash::what() const noexcept {
  return msg_.c_str();
}

void Block::Fetch(bool) {
  data_ = device_->ReadBlock(ToDeviceSector(block_number_), static_cast<uint32_t>(data_.size()), iv_, encrypted_);
}

void Block::Flush() {
  device_->WriteBlock(ToDeviceSector(block_number_), data_, iv_, encrypted_);
}

uint32_t Block::ToDeviceSector(uint32_t block_number) {
  return block_number << (BlockSize::Basic - device_->GetDevice()->GetLog2SectorSize());
}
