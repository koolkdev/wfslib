/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>

#include "block.h"
#include "file_layout.h"
#include "structs.h"
#include "utils.h"

struct FileDataBlockLocation {
  uint32_t block_number;
  BlockType block_type;
  Block::HashRef hash;
};

template <typename T>
std::span<const T> EntryMetadataItems(const EntryMetadata* metadata, size_t count) {
  const auto* metadata_bytes = reinterpret_cast<const std::byte*>(metadata);
  const auto metadata_size = sizeof(T) * count;
  const auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
  return {reinterpret_cast<const T*>(end - metadata_size), count};
}

template <typename T>
std::span<T> MutableEntryMetadataItems(EntryMetadata* metadata, size_t count) {
  auto* metadata_bytes = reinterpret_cast<std::byte*>(metadata);
  const auto metadata_size = sizeof(T) * count;
  auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
  return {reinterpret_cast<T*>(end - metadata_size), count};
}

template <FileLayoutCategory Category>
struct FileDataUnitLayoutTraits;

template <BlockType UnitBlockType>
struct SingleBlockFileDataUnitLayoutTraits {
  using Metadata = DataBlockMetadata;

  static constexpr BlockType kAllocationBlockType = UnitBlockType;
  static constexpr BlockType kDataBlockType = UnitBlockType;
  static constexpr size_t kDataBlocksPerUnit = 1;

  static uint32_t unit_block_number(const Metadata& metadata) { return metadata.block_number.value(); }

  static void set_unit_block_number(Metadata& metadata, uint32_t block_number) { metadata.block_number = block_number; }

  static FileDataBlockLocation data_block_location_for_unit(const std::shared_ptr<Block>& metadata_block,
                                                            const Metadata& metadata,
                                                            [[maybe_unused]] size_t block_index) {
    assert(block_index == 0);
    return {metadata.block_number.value(), kDataBlockType, {metadata_block, metadata_block->to_offset(metadata.hash)}};
  }
};

template <>
struct FileDataUnitLayoutTraits<FileLayoutCategory::Blocks> : SingleBlockFileDataUnitLayoutTraits<BlockType::Single> {};

template <>
struct FileDataUnitLayoutTraits<FileLayoutCategory::LargeBlocks>
    : SingleBlockFileDataUnitLayoutTraits<BlockType::Large> {};

template <>
struct FileDataUnitLayoutTraits<FileLayoutCategory::Clusters> {
  using Metadata = DataBlocksClusterMetadata;

  static constexpr BlockType kAllocationBlockType = BlockType::Cluster;
  static constexpr BlockType kDataBlockType = BlockType::Large;
  static constexpr size_t kDataBlocksPerUnit = (size_t{1} << log2_size(BlockType::Cluster)) >>
                                               log2_size(kDataBlockType);

  static uint32_t unit_block_number(const Metadata& metadata) { return metadata.block_number.value(); }

  static void set_unit_block_number(Metadata& metadata, uint32_t block_number) { metadata.block_number = block_number; }

  static FileDataBlockLocation data_block_location_for_unit(const std::shared_ptr<Block>& metadata_block,
                                                            const Metadata& metadata,
                                                            size_t block_index) {
    assert(block_index < kDataBlocksPerUnit);
    return {metadata.block_number.value() + static_cast<uint32_t>(block_index << log2_size(kDataBlockType)),
            kDataBlockType,
            {metadata_block, metadata_block->to_offset(metadata.hash[block_index])}};
  }
};

template <FileLayoutCategory Category>
constexpr uint32_t FileDataUnitAreaBlocksCount() {
  return uint32_t{1} << log2_size(FileDataUnitLayoutTraits<Category>::kAllocationBlockType);
}

template <FileLayoutCategory Category>
size_t FileDataBlockLog2Size(uint8_t block_size_log2) {
  return block_size_log2 + log2_size(FileDataUnitLayoutTraits<Category>::kDataBlockType);
}

template <FileLayoutCategory Category>
auto FileDataUnitLogicalMetadataItems(const EntryMetadata* metadata, size_t count) {
  using Metadata = typename FileDataUnitLayoutTraits<Category>::Metadata;
  // WFS stores data-unit metadata from the end of the entry allocation backwards. Expose it in file-offset order so
  // callers can index by logical unit number.
  return EntryMetadataItems<Metadata>(metadata, count) | std::views::reverse;
}

template <FileLayoutCategory Category>
auto FileDataUnitLogicalMetadataItemsForLayout(const EntryMetadata* metadata, uint8_t block_size_log2) {
  return FileDataUnitLogicalMetadataItems<Category>(
      metadata,
      static_cast<size_t>(FileLayout::MetadataItemsCount(Category, metadata->size_on_disk.value(), block_size_log2)));
}

template <FileLayoutCategory Category>
auto MutableFileDataUnitLogicalMetadataItems(EntryMetadata* metadata, size_t count) {
  using Metadata = typename FileDataUnitLayoutTraits<Category>::Metadata;
  return MutableEntryMetadataItems<Metadata>(metadata, count) | std::views::reverse;
}

inline auto ClusterMetadataBlockRefs(const EntryMetadata* metadata, size_t count) {
  return EntryMetadataItems<uint32_be_t>(metadata, count) | std::views::reverse;
}

inline auto MutableClusterMetadataBlockRefs(EntryMetadata* metadata, size_t count) {
  return MutableEntryMetadataItems<uint32_be_t>(metadata, count) | std::views::reverse;
}

inline std::span<DataBlocksClusterMetadata> MutableClusterMetadataBlockItems(
    const std::shared_ptr<Block>& metadata_block,
    size_t clusters_per_metadata_block) {
  return {metadata_block->get_mutable_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader)),
          clusters_per_metadata_block};
}

inline std::span<const DataBlocksClusterMetadata> ClusterMetadataBlockItems(
    const std::shared_ptr<Block>& metadata_block,
    size_t clusters_per_metadata_block) {
  return {metadata_block->get_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader)),
          clusters_per_metadata_block};
}

inline size_t ClusterMetadataBlockIndexForDataBlock(size_t data_block_index, uint8_t block_size_log2) {
  const auto large_blocks_per_cluster = FileDataUnitLayoutTraits<FileLayoutCategory::Clusters>::kDataBlocksPerUnit;
  const auto clusters_per_metadata_block = FileLayout::ClustersPerClusterMetadataBlock(block_size_log2);
  return data_block_index / large_blocks_per_cluster / clusters_per_metadata_block;
}

inline size_t ClusterMetadataBlockDataBlockIndex(size_t data_block_index, uint8_t block_size_log2) {
  const auto large_blocks_per_cluster = FileDataUnitLayoutTraits<FileLayoutCategory::Clusters>::kDataBlocksPerUnit;
  const auto clusters_per_metadata_block = FileLayout::ClustersPerClusterMetadataBlock(block_size_log2);
  return data_block_index % (large_blocks_per_cluster * clusters_per_metadata_block);
}

template <FileLayoutCategory Category, std::ranges::random_access_range Units>
FileDataBlockLocation FileDataBlockLocationForLogicalMetadata(const std::shared_ptr<Block>& metadata_block,
                                                              Units&& units,
                                                              size_t data_block_index) {
  using Traits = FileDataUnitLayoutTraits<Category>;

  const auto unit_index = data_block_index / Traits::kDataBlocksPerUnit;
  const auto block_index = data_block_index % Traits::kDataBlocksPerUnit;
  return Traits::data_block_location_for_unit(metadata_block, std::ranges::begin(units)[unit_index], block_index);
}

inline FileDataBlockLocation ClusterMetadataBlockDataBlockLocationFor(const std::shared_ptr<Block>& metadata_block,
                                                                      size_t data_block_index,
                                                                      uint8_t block_size_log2) {
  const auto clusters_per_metadata_block = FileLayout::ClustersPerClusterMetadataBlock(block_size_log2);
  assert(data_block_index <
         clusters_per_metadata_block * FileDataUnitLayoutTraits<FileLayoutCategory::Clusters>::kDataBlocksPerUnit);
  const auto cluster_metadata = ClusterMetadataBlockItems(metadata_block, clusters_per_metadata_block);
  return FileDataBlockLocationForLogicalMetadata<FileLayoutCategory::Clusters>(metadata_block, cluster_metadata,
                                                                               data_block_index);
}

inline FileDataBlockLocation ClusterMetadataBlocksDataBlockLocationFor(
    std::span<const std::shared_ptr<Block>> metadata_blocks,
    size_t data_block_index,
    uint8_t block_size_log2) {
  const auto metadata_block_index = ClusterMetadataBlockIndexForDataBlock(data_block_index, block_size_log2);
  return ClusterMetadataBlockDataBlockLocationFor(metadata_blocks[metadata_block_index],
                                                  ClusterMetadataBlockDataBlockIndex(data_block_index, block_size_log2),
                                                  block_size_log2);
}

template <FileLayoutCategory Category>
FileDataBlockLocation FileDataBlockLocationFor(const std::shared_ptr<Block>& metadata_block,
                                               const EntryMetadata* metadata,
                                               size_t data_block_index,
                                               uint8_t block_size_log2) {
  auto units = FileDataUnitLogicalMetadataItemsForLayout<Category>(metadata, block_size_log2);
  return FileDataBlockLocationForLogicalMetadata<Category>(metadata_block, units, data_block_index);
}

inline FileDataBlockLocation FileDataBlockLocationFor(FileLayoutCategory category,
                                                      const std::shared_ptr<Block>& metadata_block,
                                                      const EntryMetadata* metadata,
                                                      size_t data_block_index,
                                                      uint8_t block_size_log2) {
  switch (category) {
    case FileLayoutCategory::Blocks:
      return FileDataBlockLocationFor<FileLayoutCategory::Blocks>(metadata_block, metadata, data_block_index,
                                                                  block_size_log2);
    case FileLayoutCategory::LargeBlocks:
      return FileDataBlockLocationFor<FileLayoutCategory::LargeBlocks>(metadata_block, metadata, data_block_index,
                                                                       block_size_log2);
    case FileLayoutCategory::Clusters:
      return FileDataBlockLocationFor<FileLayoutCategory::Clusters>(metadata_block, metadata, data_block_index,
                                                                    block_size_log2);
    case FileLayoutCategory::Inline:
    case FileLayoutCategory::ClusterMetadataBlocks:
      break;
  }
  throw std::invalid_argument("File layout category does not store data-unit metadata in entry metadata");
}
