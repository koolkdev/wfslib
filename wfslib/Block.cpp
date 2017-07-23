/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "Block.h"
#include "DeviceEncryption.h"
#include "Device.h"
#include <boost/format.hpp>

Block::BadHash::BadHash(uint32_t block_number) : block_number(block_number),
	msg((boost::format("Bad hash for block 0x%08X") % block_number).str()) {
}
char const* Block::BadHash::what() const {
	return msg.c_str();
}

void Block::Fetch() {
	this->data = std::move(this->device->ReadBlock(ToDeviceSector(this->block_number), this->data_size, this->iv));
}

void Block::Flush() {
	this->device->WriteBlock(ToDeviceSector(this->block_number), this->data, this->iv);
}

uint32_t Block::ToDeviceSector(uint32_t block_number) {
	return block_number << (BlockSize::Basic - this->device->GetDevice()->GetLog2SectorSize());
}