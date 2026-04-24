/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <cstdint>

enum class FileLayoutMode {
  MinimumForGrow,
  MaximumForShrink,
};

struct FileLayout {
  uint8_t size_category;
  uint8_t metadata_log2_size;
  uint32_t file_size;
  uint32_t size_on_disk;
  uint32_t data_units_count;
};

size_t FileLayoutBaseMetadataSize(uint8_t filename_length);
uint32_t FileLayoutInlineCapacity(uint8_t filename_length);
uint32_t FileLayoutDataUnitsCount(uint8_t size_category, uint32_t size_on_disk, uint8_t block_size_log2);
uint32_t FileLayoutMetadataItemsCount(uint8_t size_category, uint32_t size_on_disk, uint8_t block_size_log2);
uint32_t FileLayoutCategory4ClustersPerMetadataBlock(uint8_t block_size_log2);
uint32_t FileLayoutCategory4MetadataBlocksCount(uint32_t clusters_count, uint8_t block_size_log2);
uint32_t FileLayoutMaxFileSize(uint8_t block_size_log2);

FileLayout CalculateFileLayout(uint32_t file_size,
                               uint8_t filename_length,
                               uint8_t block_size_log2,
                               FileLayoutMode mode);
