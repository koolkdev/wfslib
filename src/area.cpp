/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "area.h"

#include <numeric>
#include <random>

#include "blocks_device.h"
#include "device.h"
#include "directory.h"
#include "free_blocks_allocator.h"
#include "metadata_block.h"
#include "structs.h"
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

// static
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

// static
std::expected<std::shared_ptr<Area>, WfsError> Area::CreateRootArea(const std::shared_ptr<BlocksDevice>& device) {
  constexpr uint32_t kTransactionsAreaEnd = 0x1000;

  auto block = MetadataBlock::LoadBlock(device, /*block_number=*/0, Block::BlockSize::Regular, /*iv=*/0,
                                        /*check_hash=*/false, /*load_data=*/false);
  if (!block.has_value()) {
    return std::unexpected(block.error());
  }

  uint32_t blocks_count =
      device->device()->SectorsCount() >> (Block::BlockSize::Regular - device->device()->Log2SectorSize());

  AttributesBlock attributes{*block, sizeof(MetadataBlockHeader) + offsetof(WfsHeader, root_area_attributes)};
  auto area = std::make_shared<Area>(device, nullptr, *block, "", attributes);

  std::random_device rand_device;
  std::default_random_engine rand_engine{rand_device()};
  std::uniform_int_distribution<uint32_t> random_iv_generator(std::numeric_limits<uint32_t>::min(),
                                                              std::numeric_limits<uint32_t>::max());

  auto* header = area->mutable_header();
  std::fill(reinterpret_cast<std::byte*>(header), reinterpret_cast<std::byte*>(header + 1), std::byte{0});
  header->iv = random_iv_generator(rand_engine);
  header->blocks_count = blocks_count;
  header->root_directory_block_number = kRootDirectoryBlockNumber;
  header->shadow_directory_block_number_1 = kShadowDirectory1BlockNumber;
  header->shadow_directory_block_number_2 = kShadowDirectory2BlockNumber;
  header->depth = 0;
  header->block_size_log2 = static_cast<uint8_t>(Block::BlockSize::Regular);
  header->large_block_size_log2 = header->block_size_log2.value() + static_cast<uint8_t>(Block::BlockSizeType::Large);
  header->large_block_cluster_size_log2 =
      header->block_size_log2.value() + static_cast<uint8_t>(Block::BlockSizeType::LargeCluster);
  header->area_type = static_cast<uint8_t>(WfsAreaHeader::AreaType::QuotaArea);
  header->maybe_always_zero = 0;
  header->remainder_blocks_count = 0;
  header->first_fragments[0].block_number = 0;
  header->first_fragments[0].blocks_count = area->ToAbsoluteBlocksCount(blocks_count);
  header->fragments_log2_block_size = static_cast<uint32_be_t>(Block::BlockSize::Basic);

  auto* wfs_header = area->mutable_wfs_header();
  std::fill(reinterpret_cast<std::byte*>(wfs_header), reinterpret_cast<std::byte*>(wfs_header + 1), std::byte{0});
  wfs_header->iv = random_iv_generator(rand_engine);
  wfs_header->device_type = static_cast<uint16_t>(DeviceType::USB);  // TODO
  wfs_header->version = WFS_VERSION;
  wfs_header->root_area_attributes.flags = Attributes::DIRECTORY | Attributes::AREA_SIZE_REGULAR | Attributes::QUOTA;
  wfs_header->root_area_attributes.blocks_count = blocks_count;
  wfs_header->transactions_area_block_number = area->ToAbsoluteBlockNumber(kTransactionsBlockNumber);
  wfs_header->transactions_area_blocks_count =
      kTransactionsAreaEnd - wfs_header->transactions_area_block_number.value();

  // Initialize FreeBlocksAllocator:
  auto free_blocks_allocator_block = area->GetMetadataBlock(kFreeBlocksAllocatorBlockNumber, /*new_block=*/true);
  if (!free_blocks_allocator_block.has_value()) {
    return std::unexpected(free_blocks_allocator_block.error());
  }
  auto free_blocks_allocator = std::make_unique<FreeBlocksAllocator>(area, std::move(*free_blocks_allocator_block));
  free_blocks_allocator->Init();

  // TODO: Initialize:
  // 1. Root directory
  // 2. shadow directories
  // 3. Transactions area

  return area;
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
  return GetDirectory(header()->root_directory_block_number.value(), root_directory_name_, root_directory_attributes_);
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetShadowDirectory1() {
  return GetDirectory(header()->shadow_directory_block_number_1.value(), ".shadow_dir_1", {});
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetShadowDirectory2() {
  return GetDirectory(header()->shadow_directory_block_number_2.value(), ".shadow_dir_2", {});
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetTransactionsArea1() const {
  if (!has_wfs_header())
    return nullptr;
  auto metadata_block =
      MetadataBlock::LoadBlock(device_, wfs_header()->transactions_area_block_number.value(), Block::BlockSize::Basic,
                               IV(wfs_header()->transactions_area_block_number.value()));
  if (!metadata_block.has_value())
    return std::unexpected(WfsError::kTransactionsAreaCorrupted);
  return std::make_shared<Area>(device_, nullptr, *metadata_block, ".transactions_area_1", AttributesBlock{});
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetTransactionsArea2() const {
  if (!has_wfs_header())
    return nullptr;
  auto metadata_block =
      MetadataBlock::LoadBlock(device_, wfs_header()->transactions_area_block_number.value() + 1,
                               Block::BlockSize::Basic, IV(wfs_header()->transactions_area_block_number.value() + 1));
  if (!metadata_block.has_value())
    return std::unexpected(WfsError::kTransactionsAreaCorrupted);
  return std::make_shared<Area>(device_, nullptr, *metadata_block, ".transactions_area_2", AttributesBlock{});
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

std::expected<std::shared_ptr<MetadataBlock>, WfsError> Area::GetMetadataBlock(uint32_t block_number,
                                                                               bool new_block) const {
  return GetMetadataBlock(block_number, static_cast<Block::BlockSize>(BlockSizeLog2()), new_block);
}

uint32_t Area::IV(uint32_t block_number) const {
  return (header()->iv.value() ^
          (root_area_ ? const_cast<const Area*>(root_area_.get()) : this)->wfs_header()->iv.value()) +
         ((block_number) << (Block::BlockSize::Basic - device_->device()->Log2SectorSize()));
}

std::expected<std::shared_ptr<MetadataBlock>, WfsError> Area::GetMetadataBlock(uint32_t block_number,
                                                                               Block::BlockSize size,
                                                                               bool new_block) const {
  return MetadataBlock::LoadBlock(device_, ToAbsoluteBlockNumber(block_number), size,
                                  IV(ToRelativeBlocksCount(block_number)), /*check_hash=*/true, !new_block);
}

std::expected<std::shared_ptr<DataBlock>, WfsError> Area::GetDataBlock(uint32_t block_number,
                                                                       Block::BlockSize size,
                                                                       uint32_t data_size,
                                                                       const DataBlock::DataBlockHash& data_hash,
                                                                       bool encrypted) const {
  return DataBlock::LoadBlock(device_, ToAbsoluteBlockNumber(block_number), size, data_size,
                              IV(ToRelativeBlocksCount(block_number)), data_hash, encrypted);
}

size_t Area::BlockSizeLog2() const {
  return header()->block_size_log2.value();
}

uint32_t Area::ToRelativeBlockNumber(uint32_t absolute_block_number) const {
  return ToRelativeBlocksCount(absolute_block_number - header_block_->BlockNumber());
}

uint32_t Area::ToAbsoluteBlockNumber(uint32_t relative_block_number) const {
  return ToAbsoluteBlocksCount(relative_block_number) + header_block_->BlockNumber();
}

uint32_t Area::ToRelativeBlocksCount(uint32_t absolute_blocks_count) const {
  return absolute_blocks_count >> (BlockSizeLog2() - Block::BlockSize::Basic);
}

uint32_t Area::ToAbsoluteBlocksCount(uint32_t relative_blocks_count) const {
  return relative_blocks_count << (BlockSizeLog2() - Block::BlockSize::Basic);
}

uint32_t Area::BlockNumber() const {
  return header_block_->BlockNumber();
}

uint32_t Area::BlocksCount() const {
  return header()->blocks_count.value();
}

std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> Area::GetFreeBlocksAllocator() {
  auto metadata_block = GetMetadataBlock(kFreeBlocksAllocatorBlockNumber);
  if (!metadata_block.has_value())
    return std::unexpected(WfsError::kFreeBlocksAllocatorCorrupted);
  return std::make_unique<FreeBlocksAllocator>(shared_from_this(), std::move(*metadata_block));
}

size_t Area::BlocksCacheSizeLog2() const {
  return (BlocksCount() >> (24 - Block::BlockSize::Basic))
             ? ((BlocksCount() >> (30 - BlockSizeLog2()) ? 23 : 21) - BlockSizeLog2())
             : 0;
}

uint32_t Area::ReservedBlocksCount() const {
  uint32_t reserved_blocks = kReservedAreaBlocks;
  if (root_area_) {
    // Non root area - reserve 6 blocks
    return reserved_blocks;
  }
  reserved_blocks += ToRelativeBlocksCount(wfs_header()->transactions_area_blocks_count.value());
  return reserved_blocks;
}

std::expected<std::shared_ptr<MetadataBlock>, WfsError> Area::AllocMetadataBlock() {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return std::unexpected(allocator.error());
  auto res = (*allocator)->AllocBlocks(1, Block::BlockSizeType::Single, true);
  if (!res)
    return std::unexpected(kNoSpace);
  return GetMetadataBlock((*res)[0], /*new_block=*/true);
}

std::expected<std::vector<uint32_t>, WfsError> Area::AllocDataBlocks(uint32_t chunks_count,
                                                                     Block::BlockSizeType chunk_size) {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return std::unexpected(allocator.error());
  auto res = (*allocator)->AllocBlocks(chunks_count, chunk_size, false);
  if (!res)
    return std::unexpected(kNoSpace);
  return *res;
}

bool Area::DeleteBlocks(uint32_t block_number, uint32_t blocks_count) {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return false;
  return (*allocator)->AddFreeBlocks({block_number, blocks_count});
}
