/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "quota_area.h"

#include <algorithm>
#include <numeric>
#include <ranges>

#include "directory.h"
#include "free_blocks_allocator.h"
#include "utils.h"
#include "wfs_device.h"

QuotaArea::QuotaArea(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block)
    : Area(std::move(wfs_device), std::move(header_block)) {}

// static
std::expected<std::shared_ptr<QuotaArea>, WfsError> QuotaArea::Create(std::shared_ptr<WfsDevice> wfs_device,
                                                                      std::shared_ptr<Area> parent_area,
                                                                      uint32_t blocks_count,
                                                                      BlockSize block_size,
                                                                      const std::vector<QuotaFragment>& fragments) {
  // TODO: size
  std::shared_ptr<Block> block;
  if (parent_area) {
    auto loaded_block = parent_area->LoadMetadataBlock(fragments[0].block_number);
    if (!loaded_block.has_value())
      return std::unexpected(loaded_block.error());
  } else {
    block = wfs_device->root_block();
  }
  auto quota = std::make_shared<QuotaArea>(wfs_device, std::move(block));
  quota->Init(parent_area, blocks_count, block_size, fragments);
  return quota;
}

std::expected<std::shared_ptr<Directory>, WfsError> QuotaArea::LoadDirectory(uint32_t area_block_number,
                                                                             std::string name,
                                                                             AttributesRef attributes) {
  auto block = LoadMetadataBlock(area_block_number);
  if (!block.has_value())
    return std::unexpected(WfsError::kDirectoryCorrupted);
  return std::make_shared<Directory>(std::move(name), std::move(attributes), shared_from_this(), std::move(*block));
}

std::expected<std::shared_ptr<Directory>, WfsError> QuotaArea::LoadRootDirectory(std::string name,
                                                                                 AttributesRef attributes) {
  return LoadDirectory(header()->root_directory_block_number.value(), std::move(name), std::move(attributes));
}

std::expected<std::shared_ptr<Directory>, WfsError> QuotaArea::GetShadowDirectory1() {
  return LoadDirectory(header()->shadow_directory_block_number_1.value(), ".shadow_dir_1", {});
}

std::expected<std::shared_ptr<Directory>, WfsError> QuotaArea::GetShadowDirectory2() {
  return LoadDirectory(header()->shadow_directory_block_number_2.value(), ".shadow_dir_2", {});
}

std::expected<std::shared_ptr<QuotaArea>, WfsError> QuotaArea::LoadQuotaArea(uint32_t area_block_number,
                                                                             BlockSize block_size) {
  auto area_metadata_block = LoadMetadataBlock(area_block_number, block_size);
  if (!area_metadata_block.has_value())
    return std::unexpected(WfsError::kAreaHeaderCorrupted);
  return std::make_shared<QuotaArea>(wfs_device(), std::move(*area_metadata_block));
}

std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> QuotaArea::GetFreeBlocksAllocator() {
  auto block = LoadMetadataBlock(kFreeBlocksAllocatorBlockNumber);
  if (!block.has_value())
    return std::unexpected(WfsError::kFreeBlocksAllocatorCorrupted);
  return std::make_unique<FreeBlocksAllocator>(shared_from_this(), std::move(*block));
}

std::expected<std::shared_ptr<Block>, WfsError> QuotaArea::AllocMetadataBlock() {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return std::unexpected(allocator.error());
  auto res = (*allocator)->AllocBlocks(1, BlockType::Single, /*use_cache=*/true);
  if (!res)
    return std::unexpected(kNoSpace);
  return LoadMetadataBlock((*res)[0], /*new_block=*/true);
}

std::expected<std::vector<uint32_t>, WfsError> QuotaArea::AllocDataBlocks(uint32_t count, BlockType type) {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return std::unexpected(allocator.error());
  auto res = (*allocator)->AllocBlocks(count, type, false);
  if (!res)
    return std::unexpected(kNoSpace);
  return *res;
}

std::expected<std::vector<QuotaArea::QuotaFragment>, WfsError> QuotaArea::AllocAreaBlocks(uint32_t blocks_count) {
  uint32_t extents_count = (blocks_count + (1 << log2_size(BlockType::Cluster)) - 1) >> log2_size(BlockType::Cluster);
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return std::unexpected(allocator.error());
  auto res = (*allocator)->AllocAreaBlocks(extents_count, BlockType::Cluster);
  if (!res)
    return std::unexpected(kNoSpace);
  return *res | std::views::transform([](const auto& frag) {
    return QuotaFragment{frag.block_number, frag.blocks_count};
  }) | std::ranges::to<std::vector>();
}

bool QuotaArea::DeleteBlocks(uint32_t block_number, uint32_t blocks_count) {
  auto allocator = GetFreeBlocksAllocator();
  if (!allocator)
    return false;
  return (*allocator)->AddFreeBlocks({block_number, blocks_count});
}

void QuotaArea::Init(std::shared_ptr<Area> parent_area,
                     uint32_t blocks_count,
                     BlockSize block_size,
                     const std::vector<QuotaFragment>& fragments) {
  Area::Init(parent_area, blocks_count, block_size);

  auto* header = mutable_header();
  header->root_directory_block_number = kRootDirectoryBlockNumber;
  header->shadow_directory_block_number_1 = kShadowDirectory1BlockNumber;
  header->shadow_directory_block_number_2 = kShadowDirectory2BlockNumber;
  header->area_type = static_cast<uint8_t>(WfsAreaHeader::AreaType::QuotaArea);

  for (const auto& [dst, src] : std::views::zip(header->first_fragments, fragments)) {
    dst.block_number = src.block_number;
    dst.blocks_count = src.blocks_count;
  }

  header->fragments_log2_block_size =
      static_cast<uint32_t>(parent_area ? parent_area->block_size_log2() : log2_size(BlockSize::Physical));
  uint32_t blocks_count_in_parent_size = to_physical_blocks_count(blocks_count);
  if (parent_area)
    blocks_count_in_parent_size = parent_area->to_area_blocks_count(blocks_count_in_parent_size);
  uint32_t total_blocks_count_in_parent_size =
      std::accumulate(fragments.begin(), fragments.end(), uint32_t{0},
                      [](auto acc, const auto& frag) { return acc + frag.blocks_count; });
  header->remainder_blocks_count =
      static_cast<uint16_t>(blocks_count_in_parent_size - total_blocks_count_in_parent_size);

  auto* quota_header = mutable_quota_header();
  quota_header->max_fragments_count = static_cast<uint16_t>(std::size(quota_header->fragments));
  quota_header->fragments_log2_block_size = static_cast<uint16_t>(header->fragments_log2_block_size.value());
  for (const auto& [dst, src] : std::views::zip(quota_header->fragments, fragments)) {
    dst.block_number = src.block_number;
    dst.blocks_count = src.blocks_count;
  }

  // Initialize FreeBlocksAllocator:
  auto free_blocks_allocator_block =
      throw_if_error(LoadMetadataBlock(kFreeBlocksAllocatorBlockNumber, /*new_block=*/true));

  auto free_blocks_allocator =
      std::make_unique<FreeBlocksAllocator>(shared_from_this(), std::move(free_blocks_allocator_block));
  auto quota_free_blocks =
      fragments | std::views::transform([&](const auto& frag) {
        return FreeBlocksRangeInfo{
            to_area_block_number(parent_area ? parent_area->to_physical_block_number(frag.block_number)
                                             : frag.block_number),
            to_area_blocks_count(parent_area ? parent_area->to_physical_blocks_count(frag.blocks_count)
                                             : frag.blocks_count)};
      }) |
      std::ranges::to<std::vector>();

  // Decrease reserved blocks from first block
  uint32_t reserved_blocks = kReservedAreaBlocks;
  if (is_root_area()) {
    // Root area also reserve for transaction
    reserved_blocks += to_area_blocks_count(wfs_device()->header()->transactions_area_blocks_count.value());
  }
  quota_free_blocks.front().block_number += reserved_blocks;
  quota_free_blocks.front().blocks_count -= reserved_blocks;

  // Decrease spare blocks from last block
  quota_free_blocks.back().blocks_count -=
      to_area_blocks_count(parent_area ? parent_area->to_physical_blocks_count(header->remainder_blocks_count.value())
                                       : header->remainder_blocks_count.value());
  free_blocks_allocator->Init(std::move(quota_free_blocks));

  // TODO: Initialize:
  // 1. Root directory
  // 2. shadow directories
}
