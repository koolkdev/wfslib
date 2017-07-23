/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "Area.h"
#include "MetadataBlock.h"
#include "Wfs.h"
#include "Directory.h"

#include "Structs.h"

#include "Device.h"
#include "DeviceEncryption.h"

WfsArea * Area::Data() {
	if (this->block_number == 0) {
		return reinterpret_cast<WfsArea *>(&this->header_block->GetData()[sizeof(MetadataBlockHeader) + sizeof(WfsHeader)]);
	}
	else {
		return reinterpret_cast<WfsArea *>(&this->header_block->GetData()[sizeof(MetadataBlockHeader)]);
	}
}

WfsHeader * Area::WfsData() {
	if (this->block_number == 0) {
		return reinterpret_cast<WfsHeader *>(&this->header_block->GetData()[sizeof(MetadataBlockHeader)]);
	}
	else {
		return NULL;
	}
}

Area::Area(Wfs* wfs, uint32_t block_number, std::shared_ptr<MetadataBlock>& block, std::string root_directory_name, AttributesBlock& root_directory_attributes) :
	wfs(wfs), block_number(block_number), header_block(block), root_directory_name(root_directory_name), root_directory_attributes(root_directory_attributes) {
}

std::shared_ptr<Area> Area::LoadRootArea(Wfs* wfs) {
	std::shared_ptr<MetadataBlock> block;
	try {
		block = MetadataBlock::LoadBlock(wfs->GetDevice(), 0, Block::BlockSize::Basic, 0);
	}
	catch (Block::BadHash) {
		block = MetadataBlock::LoadBlock(wfs->GetDevice(), 0, Block::BlockSize::Regular, 0);
	}
	return std::make_shared<Area>(wfs, 0, block, "", AttributesBlock {block, sizeof(MetadataBlockHeader) + offsetof(WfsHeader, root_area_attributes)});
}

std::shared_ptr<Directory> Area::GetDirectory(uint32_t block_number, std::string name, AttributesBlock& attributes) {
	// TODO: Cache
	return std::make_shared<Directory>(name, attributes, shared_from_this(), GetMetadataBlock(block_number));
}

std::shared_ptr<Directory> Area::GetRootDirectory() {
	return GetDirectory(this->Data()->header.root_directory_block_number.value(), root_directory_name, root_directory_attributes);
}

std::shared_ptr<Area> Area::GetArea(uint32_t block_number, std::string root_directory_name, AttributesBlock& root_directory_attributes, Block::BlockSize size) {
	return std::make_shared<Area>(this->wfs, this->block_number + ToBasicBlockNumber(block_number), GetMetadataBlock(block_number, size), root_directory_name, root_directory_attributes);
}

std::shared_ptr<MetadataBlock> Area::GetMetadataBlock(uint32_t block_number) {
	return GetMetadataBlock(block_number, static_cast<Block::BlockSize>(this->Data()->header.log2_block_size.value()));
}

uint32_t Area::IV(uint32_t block_number) {
	return (this->Data()->header.iv.value() ^ this->wfs->root_area->WfsData()->iv.value()) +
		(ToBasicBlockNumber(block_number) << (Block::BlockSize::Basic - this->wfs->GetDevice()->GetDevice()->GetLog2SectorSize()));
}

std::shared_ptr<MetadataBlock> Area::GetMetadataBlock(uint32_t block_number, Block::BlockSize size) {
	return MetadataBlock::LoadBlock(this->wfs->GetDevice(), this->block_number + ToBasicBlockNumber(block_number), size, IV(block_number));
}

std::shared_ptr<DataBlock> Area::GetDataBlock(uint32_t block_number, Block::BlockSize size, uint32_t data_size, DataBlock::DataBlockHash& data_hash) {
	return DataBlock::LoadBlock(this->wfs->GetDevice(), this->block_number + ToBasicBlockNumber(block_number), size, data_size, IV(block_number), data_hash);
}

uint32_t Area::ToBasicBlockNumber(uint32_t block_number) {
	return block_number << (this->Data()->header.log2_block_size.value() - Block::BlockSize::Basic);
}

size_t Area::GetDataBlockLog2Size() {
	return this->Data()->header.log2_block_size.value();
}

uint32_t Area::GetBlockNumber(std::shared_ptr<Block> block) {
	return (block->GetBlockNumber() - this->block_number) >> (this->Data()->header.log2_block_size.value() - Block::BlockSize::Basic);
}