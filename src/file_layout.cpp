/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_layout.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <limits>
#include <stdexcept>

#include "block.h"
#include "errors.h"
#include "structs.h"
#include "utils.h"

namespace {
constexpr uint8_t kMinMetadataLog2Size = 6;
constexpr uint8_t kMaxMetadataLog2Size = 10;
constexpr uint8_t kInlineMaxMetadataLog2Size = 9;
constexpr uint32_t kBlocksMaxCount = 5;
constexpr uint32_t kLargeBlocksMaxCount = 5;
constexpr uint32_t kClustersMaxCount = 4;
constexpr uint32_t kClusterMetadataBlocksMaxCount = 237;

size_t DataBlockLog2Size(uint8_t block_size_log2, BlockType block_type) {
  return static_cast<size_t>(block_size_log2) + static_cast<size_t>(log2_size(block_type));
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
    throw std::invalid_argument("File layout metadata exceeds maximum WFS metadata allocation size");
  return log2_size;
}

uint32_t CategoryDataUnitSize(FileLayoutCategory category, uint8_t block_size_log2) {
  switch (category) {
    case FileLayoutCategory::Inline:
    case FileLayoutCategory::Blocks:
      return pow2<uint32_t>(block_size_log2);
    case FileLayoutCategory::LargeBlocks:
      return pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
    case FileLayoutCategory::Clusters:
    case FileLayoutCategory::ClusterMetadataBlocks:
      return pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));
  }
  throw std::invalid_argument("Unexpected file layout category");
}

size_t CategoryMetadataSize(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2) {
  const auto metadata_items_count = FileLayout::MetadataItemsCount(category, size_on_disk, block_size_log2);

  switch (category) {
    case FileLayoutCategory::Inline:
      return metadata_items_count * sizeof(std::byte);
    case FileLayoutCategory::Blocks:
    case FileLayoutCategory::LargeBlocks:
      return metadata_items_count * sizeof(DataBlockMetadata);
    case FileLayoutCategory::Clusters:
      return metadata_items_count * sizeof(DataBlocksClusterMetadata);
    case FileLayoutCategory::ClusterMetadataBlocks:
      return metadata_items_count * sizeof(uint32_be_t);
  }
  throw std::invalid_argument("Unexpected file layout category");
}

FileLayout BuildLayout(uint32_t file_size,
                       uint8_t filename_length,
                       uint8_t block_size_log2,
                       FileLayoutCategory category) {
  const auto single_block_size = pow2<uint32_t>(block_size_log2);
  const auto large_block_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
  const auto cluster_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));

  uint32_t size_on_disk = file_size;
  switch (category) {
    case FileLayoutCategory::Inline:
      break;
    case FileLayoutCategory::Blocks:
      size_on_disk = RoundUpToUnit(file_size, single_block_size);
      break;
    case FileLayoutCategory::LargeBlocks:
      size_on_disk = RoundUpToUnit(file_size, large_block_size);
      break;
    case FileLayoutCategory::Clusters:
    case FileLayoutCategory::ClusterMetadataBlocks:
      size_on_disk = RoundUpToUnit(file_size, cluster_size);
      break;
  }
  const auto data_units_count = FileLayout::DataUnitsCount(category, size_on_disk, block_size_log2);

  const auto metadata_size =
      FileLayout::BaseMetadataSize(filename_length) + CategoryMetadataSize(category, size_on_disk, block_size_log2);
  auto metadata_log2_size = MetadataLog2Size(metadata_size);
  assert(category != FileLayoutCategory::Inline || metadata_log2_size <= kInlineMaxMetadataLog2Size);

  return FileLayout{
      .category = category,
      .metadata_log2_size = metadata_log2_size,
      .file_size = file_size,
      .size_on_disk = size_on_disk,
      .data_units_count = data_units_count,
  };
}

FileLayoutCategory MinimumCategory(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2) {
  if (file_size > FileLayout::MaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);

  const auto single_block_size = pow2<uint32_t>(block_size_log2);
  const auto large_block_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
  const auto cluster_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));

  if (file_size <= FileLayout::InlineCapacity(filename_length))
    return FileLayoutCategory::Inline;
  if (file_size <= kBlocksMaxCount * single_block_size)
    return FileLayoutCategory::Blocks;
  if (file_size <= kLargeBlocksMaxCount * large_block_size)
    return FileLayoutCategory::LargeBlocks;
  if (file_size <= kClustersMaxCount * cluster_size)
    return FileLayoutCategory::Clusters;
  return FileLayoutCategory::ClusterMetadataBlocks;
}

FileLayoutCategory MaximumCategory(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2) {
  if (file_size > FileLayout::MaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);

  const auto single_block_size = pow2<uint32_t>(block_size_log2);
  const auto large_block_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
  const auto cluster_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));

  if (file_size >= cluster_size)
    return FileLayoutCategory::ClusterMetadataBlocks;
  if (file_size > large_block_size)
    return FileLayoutCategory::Clusters;
  if (file_size > single_block_size)
    return FileLayoutCategory::LargeBlocks;
  if (file_size > FileLayout::InlineCapacity(filename_length))
    return FileLayoutCategory::Blocks;
  return FileLayoutCategory::Inline;
}
}  // namespace

size_t FileLayout::BaseMetadataSize(uint8_t filename_length) {
  return offsetof(EntryMetadata, case_bitmap) + div_ceil(filename_length, 8);
}

uint32_t FileLayout::InlineCapacity(uint8_t filename_length) {
  return static_cast<uint32_t>(pow2<uint32_t>(kInlineMaxMetadataLog2Size) -
                               FileLayout::BaseMetadataSize(filename_length));
}

FileLayoutCategory FileLayout::CategoryFromValue(uint8_t value) {
  switch (value) {
    case 0:
      return FileLayoutCategory::Inline;
    case 1:
      return FileLayoutCategory::Blocks;
    case 2:
      return FileLayoutCategory::LargeBlocks;
    case 3:
      return FileLayoutCategory::Clusters;
    case 4:
      return FileLayoutCategory::ClusterMetadataBlocks;
    default:
      throw std::invalid_argument("Unexpected file layout category");
  }
}

uint8_t FileLayout::CategoryValue(FileLayoutCategory category) {
  return static_cast<uint8_t>(category);
}

uint32_t FileLayout::DataUnitsCount(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2) {
  if (category == FileLayoutCategory::Inline)
    return 0;
  return static_cast<uint32_t>(div_ceil(size_on_disk, CategoryDataUnitSize(category, block_size_log2)));
}

uint32_t FileLayout::MetadataItemsCount(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2) {
  const auto data_units_count = FileLayout::DataUnitsCount(category, size_on_disk, block_size_log2);
  if (category == FileLayoutCategory::ClusterMetadataBlocks)
    return FileLayout::ClusterMetadataBlocksCount(data_units_count, block_size_log2);
  return category == FileLayoutCategory::Inline ? size_on_disk : data_units_count;
}

uint32_t FileLayout::ClustersPerClusterMetadataBlock(uint8_t block_size_log2) {
  return static_cast<uint32_t>(std::min(
      (pow2<size_t>(block_size_log2) - sizeof(MetadataBlockHeader)) / sizeof(DataBlocksClusterMetadata), size_t{48}));
}

uint32_t FileLayout::ClusterMetadataBlocksCount(uint32_t clusters_count, uint8_t block_size_log2) {
  return static_cast<uint32_t>(div_ceil(clusters_count, FileLayout::ClustersPerClusterMetadataBlock(block_size_log2)));
}

uint32_t FileLayout::MaxFileSize(uint8_t block_size_log2) {
  const auto cluster_size = pow2<uint64_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));
  const auto max_by_size_on_disk = pow2<uint64_t>(32) - cluster_size;
  const auto max_by_cluster_metadata_blocks = uint64_t{kClusterMetadataBlocksMaxCount} *
                                              FileLayout::ClustersPerClusterMetadataBlock(block_size_log2) *
                                              cluster_size;
  return static_cast<uint32_t>(std::min(max_by_size_on_disk, max_by_cluster_metadata_blocks));
}

FileLayout FileLayout::Calculate(uint32_t file_size,
                                 uint8_t filename_length,
                                 uint8_t block_size_log2,
                                 FileLayoutMode mode) {
  const auto category = mode == FileLayoutMode::MinimumForGrow
                            ? MinimumCategory(file_size, filename_length, block_size_log2)
                            : MaximumCategory(file_size, filename_length, block_size_log2);
  return BuildLayout(file_size, filename_length, block_size_log2, category);
}
