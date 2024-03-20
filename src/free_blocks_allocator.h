/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "block.h"

class EPTree;
class Area;
class MetadataBlock;
struct FreeBlocksAllocatorHeader;

struct FreeBlocksExtentInfo {
  uint32_t block_number;
  uint32_t blocks_count;
  size_t bucket_index;

  uint32_t end_block_number() const { return block_number + blocks_count; }
};

struct FreeBlocksRangeInfo {
  uint32_t block_number;
  uint32_t blocks_count;

  uint32_t end_block_number() const { return block_number + blocks_count; }
};

class FreeBlocksAllocator {
 public:
  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block);

  uint32_t AllocFreeBlockFromCache();
  uint32_t FindSmallestFreeBlockExtent(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated);

  std::optional<std::vector<uint32_t>> AllocBlocks(uint32_t chunks_count, Block::BlockSizeType size, bool use_cache);
  std::optional<std::vector<FreeBlocksRangeInfo>> AllocAreaBlocks(uint32_t chunks_count, Block::BlockSizeType size);

  // Mark the blocks as frees by adding them to the tree
  void AddFreeBlocks(FreeBlocksRangeInfo range);

  // Remove blocks from the tree
  bool RemoveFreeBlocksExtent(FreeBlocksExtentInfo info);

  // Return whether any part of the block is freed
  bool IsRangeIsFree(FreeBlocksRangeInfo range);

  std::shared_ptr<MetadataBlock> LoadAllocatorBlock(uint32_t block_number, bool new_block = false);

  std::shared_ptr<MetadataBlock> root_block() const { return block_; }

 private:
  bool AllocBlocksOfSpecificSize(uint32_t blocks_count,
                                 size_t size_index,
                                 size_t max_size_index,
                                 std::vector<uint32_t>& result);

  // Mark the blocks as frees by adding them to a specific size tree
  void AddFreeBlocksForSize(FreeBlocksRangeInfo range, size_t bucket_index);

  // Add new single extent to the tree
  bool AddFreeBlocksExtent(FreeBlocksExtentInfo info);

  void RecreateEPTreeIfNeeded();
  bool ReplanishBlocksCache();

  std::unique_ptr<EPTree> GetEPTree();
  const FreeBlocksAllocatorHeader* header();
  FreeBlocksAllocatorHeader* mutable_header();

  std::shared_ptr<Area> area_;
  std::shared_ptr<MetadataBlock> block_;
};
