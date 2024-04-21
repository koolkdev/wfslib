/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "area.h"

#include <numeric>
#include <random>

#include "directory.h"
#include "free_blocks_allocator.h"
#include "wfs.h"

Area::Area(std::shared_ptr<Wfs> wfs_device, std::shared_ptr<Block> header_block)
    : wfs_device_(std::move(wfs_device)), header_block_(std::move(header_block)) {}

// static
#if 0
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
#endif

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetDirectory(uint32_t device_block_number,
                                                                       const std::string& name,
                                                                       const AttributesBlock& attributes) {
  auto block = LoadMetadataBlock(device_block_number);
  if (!block.has_value())
    return std::unexpected(WfsError::kDirectoryCorrupted);
  return std::make_shared<Directory>(name, attributes, shared_from_this(), std::move(*block));
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetRootDirectory(const std::string& name,
                                                                           const AttributesBlock& attributes) {
  return GetDirectory(header()->root_directory_block_number.value(), name, attributes);
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetShadowDirectory1() {
  return GetDirectory(header()->shadow_directory_block_number_1.value(), ".shadow_dir_1", {});
}

std::expected<std::shared_ptr<Directory>, WfsError> Area::GetShadowDirectory2() {
  return GetDirectory(header()->shadow_directory_block_number_2.value(), ".shadow_dir_2", {});
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetArea(uint32_t device_block_number, Block::BlockSize size) {
  auto area_metadata_block = LoadMetadataBlock(device_block_number, size);
  if (!area_metadata_block.has_value())
    return std::unexpected(WfsError::kAreaHeaderCorrupted);
  return std::make_shared<Area>(wfs_device_, std::move(*area_metadata_block));
}

std::expected<std::shared_ptr<Block>, WfsError> Area::LoadMetadataBlock(uint32_t area_block_number,
                                                                        bool new_block) const {
  return LoadMetadataBlock(area_block_number, static_cast<Block::BlockSize>(block_size_log2()), new_block);
}

std::expected<std::shared_ptr<Block>, WfsError> Area::LoadMetadataBlock(uint32_t area_block_number,
                                                                        Block::BlockSize size,
                                                                        bool new_block) const {
  return wfs_device_->LoadMetadataBlock(this, to_device_block_number(area_block_number), size, new_block);
}

std::expected<std::shared_ptr<Block>, WfsError> Area::LoadDataBlock(uint32_t area_block_number,
                                                                    Block::BlockSize size,
                                                                    uint32_t data_size,
                                                                    Block::HashRef data_hash,
                                                                    bool encrypted,
                                                                    bool new_block) const {
  return wfs_device_->LoadDataBlock(this, to_device_block_number(area_block_number), size, data_size,
                                    std::move(data_hash), encrypted, new_block);
}

std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> Area::GetFreeBlocksAllocator() {
  auto block = LoadMetadataBlock(kFreeBlocksAllocatorBlockNumber);
  if (!block.has_value())
    return std::unexpected(WfsError::kFreeBlocksAllocatorCorrupted);
  return std::make_unique<FreeBlocksAllocator>(shared_from_this(), std::move(*block));
}

uint32_t Area::ReservedBlocksCount() const {
  uint32_t reserved_blocks = kReservedAreaBlocks;
  if (is_root_area()) {
    // Root area also reserve for transaction
    reserved_blocks += to_area_blocks_count(wfs_device_->header()->transactions_area_blocks_count.value());
  }
  return reserved_blocks;
}

std::expected<std::shared_ptr<Block>, WfsError> Area::AllocMetadataBlock() {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return std::unexpected(allocator.error());
  auto res = (*allocator)->AllocBlocks(1, Block::BlockSizeType::Single, true);
  if (!res)
    return std::unexpected(kNoSpace);
  return LoadMetadataBlock((*res)[0], /*new_block=*/true);
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
