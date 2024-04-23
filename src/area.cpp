/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "area.h"

#include <numeric>
#include <random>

#include "wfs_device.h"

Area::Area(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block)
    : wfs_device_(std::move(wfs_device)), header_block_(std::move(header_block)) {}

void Area::Init(std::shared_ptr<Area> parent_area, uint32_t blocks_count, Block::BlockSize block_size) {
  std::random_device rand_device;
  std::default_random_engine rand_engine{rand_device()};
  std::uniform_int_distribution<uint32_t> random_iv_generator(std::numeric_limits<uint32_t>::min(),
                                                              std::numeric_limits<uint32_t>::max());

  auto* header = mutable_header();
  std::fill(reinterpret_cast<std::byte*>(header), reinterpret_cast<std::byte*>(header + 1), std::byte{0});
  header->iv = random_iv_generator(rand_engine);
  header->blocks_count = blocks_count;
  header->depth = parent_area ? parent_area->header()->depth.value() + 1 : 0;
  header->block_size_log2 = static_cast<uint8_t>(block_size);
  header->large_block_size_log2 = header->block_size_log2.value() + static_cast<uint8_t>(Block::BlockSizeType::Large);
  header->large_block_cluster_size_log2 =
      header->block_size_log2.value() + static_cast<uint8_t>(Block::BlockSizeType::LargeCluster);
  header->maybe_always_zero = 0;
  header->remainder_blocks_count = 0;
}

std::expected<std::shared_ptr<Area>, WfsError> Area::GetArea(uint32_t area_block_number, Block::BlockSize size) {
  auto area_metadata_block = LoadMetadataBlock(area_block_number, size);
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
