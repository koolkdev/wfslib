/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <cstdint>

enum class FileLayoutCategory : uint8_t {
  Inline = 0,
  Blocks = 1,
  LargeBlocks = 2,
  Clusters = 3,
  ClusterMetadataBlocks = 4,
};

struct FileLayout {
  FileLayoutCategory category;
  uint8_t metadata_log2_size;
  uint32_t file_size;
  uint32_t size_on_disk;
  uint32_t data_units_count;

  static size_t BaseMetadataSize(uint8_t filename_length);
  static uint32_t InlineCapacity(uint8_t filename_length);
  static FileLayoutCategory CategoryFromValue(uint8_t value);
  static uint8_t CategoryValue(FileLayoutCategory category);
  static uint32_t DataUnitsCount(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2);
  static uint32_t MetadataItemsCount(FileLayoutCategory category, uint32_t size_on_disk, uint8_t block_size_log2);
  static uint32_t ClustersPerClusterMetadataBlock(uint8_t block_size_log2);
  static uint32_t ClusterMetadataBlocksCount(uint32_t clusters_count, uint8_t block_size_log2);
  static uint32_t MaxFileSize(uint8_t block_size_log2);

  static FileLayout Calculate(uint32_t old_file_size,
                              uint32_t target_file_size,
                              uint8_t filename_length,
                              uint8_t block_size_log2,
                              FileLayoutCategory current_category = FileLayoutCategory::Inline);
};
