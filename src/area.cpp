/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "area.h"

#include "blocks_device.h"
#include "device.h"
#include "directory.h"
#include "free_blocks_allocator.h"
#include "metadata_block.h"
#include "structs.h"
#include "utils.h"
#include "wfs.h"

Area::Area(const std::shared_ptr<BlocksDevice>& device,
           const std::shared_ptr<Area>& root_area,
           const std::shared_ptr<MetadataBlock>& block,
           const std::string& root_directory_name,
           const AttributesBlock& root_directory_attributes)
    : device_(device),
      root_area_(root_area),
      header_block_(block),
      root_directory_name_(root_directory_name),
      root_directory_attributes_(root_directory_attributes) {}

std::expected<std::shared_ptr<Area>, WfsError> Area::LoadRootArea(const std::shared_ptr<BlocksDevice>& device) {
  auto block = MetadataBlock::LoadBlock(device, 0, Block::BlockSize::Basic, 0);
  if (!block.has_value()) {
    block = MetadataBlock::LoadBlock(device, 0, Block::BlockSize::Regular, 0);
    if (!block.has_value())
      return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }
  return std::make_shared<Area>(
      device, nullptr, *block, "",
      AttributesBlock{*block, sizeof(MetadataBlockHeader) + offsetof(WfsHeader, root_area_attributes)});
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetDirectory(uint32_t block_number,
                                                                       const std::string& name,
                                                                       const AttributesBlock& attributes) {
  auto block = GetMetadataBlock(block_number);
  if (!block.has_value())
    return std::unexpected(WfsError::kDirectoryCorrupted);
  return std::make_shared<Directory>(name, attributes, shared_from_this(), std::move(*block));
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetRootDirectory() {
  return GetDirectory(as_const(this)->header()->root_directory_block_number.value(), root_directory_name_,
                      root_directory_attributes_);
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetShadowDirectory1() {
  return GetDirectory(as_const(this)->header()->shadow_directory_block_number_1.value(), ".shadow_dir_1", {});
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetShadowDirectory2() {
  return GetDirectory(as_const(this)->header()->shadow_directory_block_number_2.value(), ".shadow_dir_2", {});
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetTransactionsArea1() const {
  if (!has_wfs_header())
    return nullptr;
  auto metadata_block = GetMetadataBlock(wfs_header()->transactions_area_block_number.value());
  if (!metadata_block.has_value())
    return std::unexpected(WfsError::kTransactionsAreaCorrupted);
  return std::make_shared<Area>(device_, nullptr, *metadata_block, ".transactions_area_1", AttributesBlock{});
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetTransactionsArea2() const {
  if (!has_wfs_header())
    return nullptr;
  auto metadata_block = GetMetadataBlock(wfs_header()->transactions_area_block_number.value());
  if (!metadata_block.has_value())
    return std::unexpected(WfsError::kTransactionsAreaCorrupted);
  return std::make_shared<Area>(device_, nullptr, *metadata_block, ".transactions_area_1", AttributesBlock{});
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetArea(uint32_t block_number,
                                                             const std::string& root_directory_name,
                                                             const AttributesBlock& root_directory_attributes,
                                                             Block::BlockSize size) {
  auto area_metadata_block = GetMetadataBlock(block_number, size);
  if (!area_metadata_block.has_value())
    return std::unexpected(WfsError::kAreaHeaderCorrupted);
  return std::make_shared<Area>(device_, root_area_ ? root_area_ : shared_from_this(), std::move(*area_metadata_block),
                                root_directory_name, root_directory_attributes);
}

std::expected<std::shared_ptr<MetadataBlock>, WfsError> Area::GetMetadataBlock(uint32_t block_number) const {
  return GetMetadataBlock(block_number, static_cast<Block::BlockSize>(header()->log2_block_size.value()));
}

uint32_t Area::IV(uint32_t block_number) const {
  return (header()->iv.value() ^ (root_area_ ? as_const(root_area_.get()) : this)->wfs_header()->iv.value()) +
         (ToBasicBlockNumber(block_number) << (Block::BlockSize::Basic - device_->device()->Log2SectorSize()));
}

std::expected<std::shared_ptr<MetadataBlock>, WfsError> Area::GetMetadataBlock(uint32_t block_number,
                                                                               Block::BlockSize size) const {
  return MetadataBlock::LoadBlock(device_, header_block_->BlockNumber() + ToBasicBlockNumber(block_number), size,
                                  IV(block_number));
}

std::expected<std::shared_ptr<DataBlock>, WfsError> Area::GetDataBlock(uint32_t block_number,
                                                                       Block::BlockSize size,
                                                                       uint32_t data_size,
                                                                       const DataBlock::DataBlockHash& data_hash,
                                                                       bool encrypted) const {
  return DataBlock::LoadBlock(device_, header_block_->BlockNumber() + ToBasicBlockNumber(block_number), size, data_size,
                              IV(block_number), data_hash, encrypted);
}

uint32_t Area::ToBasicBlockNumber(uint32_t block_number) const {
  return block_number << (header()->log2_block_size.value() - Block::BlockSize::Basic);
}

size_t Area::GetDataBlockLog2Size() const {
  return header()->log2_block_size.value();
}

uint32_t Area::RelativeBlockNumber(uint32_t block_number) const {
  return (block_number - header_block_->BlockNumber()) >> (header()->log2_block_size.value() - Block::BlockSize::Basic);
}

uint32_t Area::AbsoluteBlockNumber(uint32_t block_number) const {
  return (block_number << (header()->log2_block_size.value() - Block::BlockSize::Basic)) + header_block_->BlockNumber();
}

uint32_t Area::BlockNumber() const {
  return header_block_->BlockNumber();
}

uint32_t Area::BlocksCount() const {
  return root_directory_attributes_.Attributes()->blocks_count.value();
}

std::expected<std::shared_ptr<const FreeBlocksAllocator>, WfsError> Area::GetFreeBlocksAllocator() const {
  auto metadata_block = GetMetadataBlock(FreeBlocksAllocatorBlockNumber);
  if (!metadata_block.has_value())
    return std::unexpected(WfsError::kFreeBlocksAllocatorCorrupted);
  return std::make_unique<FreeBlocksAllocator>(shared_from_this(), std::move(*metadata_block));
}

std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> Area::GetFreeBlocksAllocator() {
  auto res = as_const(this)->GetFreeBlocksAllocator();
  if (!res.has_value())
    return std::unexpected(res.error());
  return std::const_pointer_cast<FreeBlocksAllocator>(*res);
}
