/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_layout.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

#include "block.h"
#include "errors.h"
#include "structs.h"
#include "utils.h"

namespace {
constexpr uint8_t kMinMetadataLog2Size = 6;
constexpr uint8_t kMaxMetadataLog2Size = 10;
constexpr uint8_t kCategory0MaxMetadataLog2Size = 9;
constexpr uint32_t kCategory1MaxSingleBlocks = 5;
constexpr uint32_t kCategory2MaxLargeBlocks = 5;
constexpr uint32_t kCategory3MaxClusters = 4;
constexpr uint32_t kCategory4MaxMetadataBlocks = 237;

uint32_t Pow2(uint8_t log2_size) {
  return uint32_t{1} << log2_size;
}

uint32_t RoundUpToUnit(uint32_t size, uint32_t unit_size) {
  const uint64_t units = (uint64_t{size} + unit_size - 1) / unit_size;
  const uint64_t rounded_size = units * unit_size;
  if (rounded_size > std::numeric_limits<uint32_t>::max())
    throw WfsException(WfsError::kFileTooLarge);
  return static_cast<uint32_t>(rounded_size);
}

uint8_t MetadataLog2Size(size_t metadata_size) {
  auto log2_size = static_cast<uint8_t>(std::max<size_t>(kMinMetadataLog2Size, std::bit_width(metadata_size - 1)));
  if (log2_size > kMaxMetadataLog2Size)
    throw WfsException(WfsError::kFileTooLarge);
  return log2_size;
}

size_t CategoryMetadataSize(uint8_t size_category,
                            uint32_t size_on_disk,
                            uint8_t block_size_log2,
                            uint32_t category4_metadata_blocks_count) {
  const auto single_block_size = Pow2(block_size_log2);
  const auto large_block_size = Pow2(block_size_log2 + log2_size(BlockType::Large));
  const auto cluster_size = Pow2(block_size_log2 + log2_size(BlockType::Cluster));

  switch (size_category) {
    case 0:
      return size_on_disk;
    case 1:
      return div_ceil(size_on_disk, single_block_size) * sizeof(DataBlockMetadata);
    case 2:
      return div_ceil(size_on_disk, large_block_size) * sizeof(DataBlockMetadata);
    case 3:
      return div_ceil(size_on_disk, cluster_size) * sizeof(DataBlocksClusterMetadata);
    case 4:
      return category4_metadata_blocks_count * sizeof(uint32_be_t);
    default:
      throw std::invalid_argument("Unexpected file layout category");
  }
}

FileLayoutSpec BuildSpec(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2, uint8_t size_category) {
  const auto single_block_size = Pow2(block_size_log2);
  const auto large_block_size = Pow2(block_size_log2 + log2_size(BlockType::Large));
  const auto cluster_size = Pow2(block_size_log2 + log2_size(BlockType::Cluster));

  uint32_t size_on_disk = file_size;
  uint32_t data_units_count = 0;
  uint32_t category4_metadata_blocks_count = 0;
  switch (size_category) {
    case 0:
      break;
    case 1:
      size_on_disk = RoundUpToUnit(file_size, single_block_size);
      data_units_count = size_on_disk / single_block_size;
      break;
    case 2:
      size_on_disk = RoundUpToUnit(file_size, large_block_size);
      data_units_count = size_on_disk / large_block_size;
      break;
    case 3:
      size_on_disk = RoundUpToUnit(file_size, cluster_size);
      data_units_count = size_on_disk / cluster_size;
      break;
    case 4:
      size_on_disk = RoundUpToUnit(file_size, cluster_size);
      data_units_count = size_on_disk / cluster_size;
      category4_metadata_blocks_count = static_cast<uint32_t>(
          div_ceil(data_units_count, FileLayoutCategory4ClustersPerMetadataBlock(block_size_log2)));
      break;
    default:
      throw std::invalid_argument("Unexpected file layout category");
  }

  const auto metadata_size =
      FileLayoutBaseMetadataSize(filename_length) +
      CategoryMetadataSize(size_category, size_on_disk, block_size_log2, category4_metadata_blocks_count);
  auto metadata_log2_size = MetadataLog2Size(metadata_size);
  if (size_category == 0 && metadata_log2_size > kCategory0MaxMetadataLog2Size)
    throw WfsException(WfsError::kFileTooLarge);

  return FileLayoutSpec{
      .size_category = size_category,
      .metadata_log2_size = metadata_log2_size,
      .file_size = file_size,
      .size_on_disk = size_on_disk,
      .data_units_count = data_units_count,
      .category4_metadata_blocks_count = category4_metadata_blocks_count,
  };
}

uint8_t MinimumCategory(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2) {
  if (file_size > FileLayoutMaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);

  const auto single_block_size = Pow2(block_size_log2);
  const auto large_block_size = Pow2(block_size_log2 + log2_size(BlockType::Large));
  const auto cluster_size = Pow2(block_size_log2 + log2_size(BlockType::Cluster));

  if (file_size <= FileLayoutInlineCapacity(filename_length))
    return 0;
  if (file_size <= kCategory1MaxSingleBlocks * single_block_size)
    return 1;
  if (file_size <= kCategory2MaxLargeBlocks * large_block_size)
    return 2;
  if (file_size <= kCategory3MaxClusters * cluster_size)
    return 3;
  return 4;
}

uint8_t MaximumCategory(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2) {
  if (file_size > FileLayoutMaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);

  const auto single_block_size = Pow2(block_size_log2);
  const auto large_block_size = Pow2(block_size_log2 + log2_size(BlockType::Large));
  const auto cluster_size = Pow2(block_size_log2 + log2_size(BlockType::Cluster));

  if (file_size >= cluster_size)
    return 4;
  if (file_size > large_block_size)
    return 3;
  if (file_size > single_block_size)
    return 2;
  if (file_size > FileLayoutInlineCapacity(filename_length))
    return 1;
  return 0;
}
}  // namespace

size_t FileLayoutBaseMetadataSize(uint8_t filename_length) {
  return offsetof(EntryMetadata, case_bitmap) + div_ceil(filename_length, 8);
}

uint32_t FileLayoutInlineCapacity(uint8_t filename_length) {
  return static_cast<uint32_t>((uint32_t{1} << kCategory0MaxMetadataLog2Size) -
                               FileLayoutBaseMetadataSize(filename_length));
}

uint32_t FileLayoutCategory4ClustersPerMetadataBlock(uint8_t block_size_log2) {
  return static_cast<uint32_t>(
      std::min((Pow2(block_size_log2) - sizeof(MetadataBlockHeader)) / sizeof(DataBlocksClusterMetadata), size_t{48}));
}

uint32_t FileLayoutMaxFileSize(uint8_t block_size_log2) {
  const auto cluster_size = uint64_t{1} << (block_size_log2 + log2_size(BlockType::Cluster));
  const auto max_by_size_on_disk = (uint64_t{1} << 32) - cluster_size;
  const auto max_by_category4_metadata = uint64_t{kCategory4MaxMetadataBlocks} *
                                         FileLayoutCategory4ClustersPerMetadataBlock(block_size_log2) * cluster_size;
  return static_cast<uint32_t>(std::min(max_by_size_on_disk, max_by_category4_metadata));
}

FileLayoutSpec CalculateFileLayout(uint32_t file_size,
                                   uint8_t filename_length,
                                   uint8_t block_size_log2,
                                   FileLayoutMode mode) {
  const auto category = mode == FileLayoutMode::MinimumForGrow
                            ? MinimumCategory(file_size, filename_length, block_size_log2)
                            : MaximumCategory(file_size, filename_length, block_size_log2);
  return BuildSpec(file_size, filename_length, block_size_log2, category);
}
