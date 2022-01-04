/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "metadata_block.h"

#include "device_encryption.h"

#include "Structs.h"

MetadataBlock::MetadataBlock(const std::shared_ptr<DeviceEncryption>& device, uint32_t block_number, Block::BlockSize size_category, uint32_t iv) :
	Block(device, block_number, size_category, iv, true, std::vector<uint8_t>(1LL << size_category, 0)) {
}

void MetadataBlock::Fetch(bool check_hash) {
	Block::Fetch();
	if (check_hash && !device_->CheckHash(data_, data_.begin() + offsetof(MetadataBlockHeader, hash), true))
		throw Block::BadHash(block_number_);
}

void MetadataBlock::Flush() {
	device_->CalculateHash(data_, data_.begin() + offsetof(MetadataBlockHeader, hash), true);
	Block::Flush();
}

std::shared_ptr<MetadataBlock> MetadataBlock::LoadBlock(const std::shared_ptr<DeviceEncryption>& device, uint32_t block_number, Block::BlockSize size_category, uint32_t iv, bool check_hash) {
	auto block = std::make_shared<MetadataBlock>(device, block_number, size_category, iv);
	block->Fetch(check_hash);
	return block;
}

MetadataBlockHeader * MetadataBlock::Header() {
	return reinterpret_cast<MetadataBlockHeader *>(&data_[0]);
}