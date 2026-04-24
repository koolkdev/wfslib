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
#include "file_layout_constants.h"
#include "structs.h"
#include "utils.h"

namespace {
using namespace file_layout_constants;

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
    case FileLayoutCategory::SingleBlock:
      return pow2<uint32_t>(block_size_log2);
    case FileLayoutCategory::LargeBlock:
      return pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
    case FileLayoutCategory::Cluster:
    case FileLayoutCategory::IndirectCluster:
      return pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));
  }
  throw std::invalid_argument("Unexpected file layout category");
}

size_t CategoryMetadataSize(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2) {
  const auto metadata_items_count = FileLayoutMetadataItemsCount(category, size_on_disk, block_size_log2);

  switch (category) {
    case FileLayoutCategory::Inline:
      return metadata_items_count * sizeof(std::byte);
    case FileLayoutCategory::SingleBlock:
    case FileLayoutCategory::LargeBlock:
      return metadata_items_count * sizeof(DataBlockMetadata);
    case FileLayoutCategory::Cluster:
      return metadata_items_count * sizeof(DataBlocksClusterMetadata);
    case FileLayoutCategory::IndirectCluster:
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
    case FileLayoutCategory::SingleBlock:
      size_on_disk = RoundUpToUnit(file_size, single_block_size);
      break;
    case FileLayoutCategory::LargeBlock:
      size_on_disk = RoundUpToUnit(file_size, large_block_size);
      break;
    case FileLayoutCategory::Cluster:
    case FileLayoutCategory::IndirectCluster:
      size_on_disk = RoundUpToUnit(file_size, cluster_size);
      break;
  }
  const auto data_units_count = FileLayoutDataUnitsCount(category, size_on_disk, block_size_log2);

  const auto metadata_size =
      FileLayoutBaseMetadataSize(filename_length) + CategoryMetadataSize(category, size_on_disk, block_size_log2);
  auto metadata_log2_size = MetadataLog2Size(metadata_size);
  if (category == FileLayoutCategory::Inline && metadata_log2_size > kCategory0MaxMetadataLog2Size)
    throw WfsException(WfsError::kFileTooLarge);

  return FileLayout{
      .category = category,
      .metadata_log2_size = metadata_log2_size,
      .file_size = file_size,
      .size_on_disk = size_on_disk,
      .data_units_count = data_units_count,
  };
}

FileLayoutCategory MinimumCategory(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2) {
  if (file_size > FileLayoutMaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);

  const auto single_block_size = pow2<uint32_t>(block_size_log2);
  const auto large_block_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
  const auto cluster_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));

  if (file_size <= FileLayoutInlineCapacity(filename_length))
    return FileLayoutCategory::Inline;
  if (file_size <= kCategory1MaxSingleBlocks * single_block_size)
    return FileLayoutCategory::SingleBlock;
  if (file_size <= kCategory2MaxLargeBlocks * large_block_size)
    return FileLayoutCategory::LargeBlock;
  if (file_size <= kCategory3MaxClusters * cluster_size)
    return FileLayoutCategory::Cluster;
  return FileLayoutCategory::IndirectCluster;
}

FileLayoutCategory MaximumCategory(uint32_t file_size, uint8_t filename_length, uint8_t block_size_log2) {
  if (file_size > FileLayoutMaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);

  const auto single_block_size = pow2<uint32_t>(block_size_log2);
  const auto large_block_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Large));
  const auto cluster_size = pow2<uint32_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));

  if (file_size >= cluster_size)
    return FileLayoutCategory::IndirectCluster;
  if (file_size > large_block_size)
    return FileLayoutCategory::Cluster;
  if (file_size > single_block_size)
    return FileLayoutCategory::LargeBlock;
  if (file_size > FileLayoutInlineCapacity(filename_length))
    return FileLayoutCategory::SingleBlock;
  return FileLayoutCategory::Inline;
}
}  // namespace

size_t FileLayoutBaseMetadataSize(uint8_t filename_length) {
  return offsetof(EntryMetadata, case_bitmap) + div_ceil(filename_length, 8);
}

uint32_t FileLayoutInlineCapacity(uint8_t filename_length) {
  return static_cast<uint32_t>(pow2<uint32_t>(kCategory0MaxMetadataLog2Size) -
                               FileLayoutBaseMetadataSize(filename_length));
}

FileLayoutCategory FileLayoutCategoryFromValue(uint8_t value) {
  switch (value) {
    case 0:
      return FileLayoutCategory::Inline;
    case 1:
      return FileLayoutCategory::SingleBlock;
    case 2:
      return FileLayoutCategory::LargeBlock;
    case 3:
      return FileLayoutCategory::Cluster;
    case 4:
      return FileLayoutCategory::IndirectCluster;
    default:
      throw std::invalid_argument("Unexpected file layout category");
  }
}

uint8_t FileLayoutCategoryValue(FileLayoutCategory category) {
  return static_cast<uint8_t>(category);
}

uint32_t FileLayoutDataUnitsCount(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2) {
  if (category == FileLayoutCategory::Inline)
    return 0;
  return static_cast<uint32_t>(div_ceil(size_on_disk, CategoryDataUnitSize(category, block_size_log2)));
}

uint32_t FileLayoutMetadataItemsCount(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2) {
  const auto data_units_count = FileLayoutDataUnitsCount(category, size_on_disk, block_size_log2);
  if (category == FileLayoutCategory::IndirectCluster)
    return FileLayoutCategory4MetadataBlocksCount(data_units_count, block_size_log2);
  return category == FileLayoutCategory::Inline ? size_on_disk : data_units_count;
}

uint32_t FileLayoutCategory4ClustersPerMetadataBlock(uint8_t block_size_log2) {
  return static_cast<uint32_t>(std::min(
      (pow2<size_t>(block_size_log2) - sizeof(MetadataBlockHeader)) / sizeof(DataBlocksClusterMetadata), size_t{48}));
}

uint32_t FileLayoutCategory4MetadataBlocksCount(uint32_t clusters_count, uint8_t block_size_log2) {
  return static_cast<uint32_t>(div_ceil(clusters_count, FileLayoutCategory4ClustersPerMetadataBlock(block_size_log2)));
}

uint32_t FileLayoutMaxFileSize(uint8_t block_size_log2) {
  const auto cluster_size = pow2<uint64_t>(DataBlockLog2Size(block_size_log2, BlockType::Cluster));
  const auto max_by_size_on_disk = pow2<uint64_t>(32) - cluster_size;
  const auto max_by_category4_metadata = uint64_t{kCategory4MaxMetadataBlocks} *
                                         FileLayoutCategory4ClustersPerMetadataBlock(block_size_log2) * cluster_size;
  return static_cast<uint32_t>(std::min(max_by_size_on_disk, max_by_category4_metadata));
}

FileLayout CalculateFileLayout(uint32_t file_size,
                               uint8_t filename_length,
                               uint8_t block_size_log2,
                               FileLayoutMode mode) {
  const auto category = mode == FileLayoutMode::MinimumForGrow
                            ? MinimumCategory(file_size, filename_length, block_size_log2)
                            : MaximumCategory(file_size, filename_length, block_size_log2);
  return BuildLayout(file_size, filename_length, block_size_log2, category);
}
