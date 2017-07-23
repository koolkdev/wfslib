/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "DataBlock.h"
#include "MetadataBlock.h"
#include "DeviceEncryption.h"

#include "Structs.h"

DataBlock::DataBlock(std::shared_ptr<DeviceEncryption>& device, uint32_t block_number, Block::BlockSize size_category, uint32_t data_size, uint32_t iv, DataBlockHash& data_hash) :
	Block(device, block_number, size_category, iv, std::move(std::vector<uint8_t>(data_size, 0)), data_size), data_hash(data_hash) {
}

void DataBlock::Fetch() {
	this->Block::Fetch();
	if (!this->device->CheckHash(data, data_hash.block->GetData().begin() + data_hash.hash_offset, false))
		throw Block::BadHash(block_number);
}

void DataBlock::Flush() {
	this->device->CalculateHash(data, data_hash.block->GetData().begin() + data_hash.hash_offset, false);
	this->Block::Flush();
}

std::shared_ptr<DataBlock> DataBlock::LoadBlock(std::shared_ptr<DeviceEncryption>& device, uint32_t block_number, Block::BlockSize size_category, uint32_t data_size, uint32_t iv, DataBlockHash& data_hash) {
	auto block = std::make_shared<DataBlock>(device, block_number, size_category, data_size, iv, data_hash);
	block->Fetch();
	return block;
}
