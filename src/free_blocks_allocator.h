/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <vector>

class EPTree;
class Area;
class MetadataBlock;
struct FreeBlocksAllocatorHeader;

struct FreeBlocksExtentInfo {
  uint32_t block_number;
  uint32_t blocks_count;
  size_t bucket_index;
};

struct FreeBlocksRangeInfo {
  uint32_t block_number;
  uint32_t blocks_count;
};

class FreeBlocksAllocator {
 public:
  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block);

  uint32_t AllocFreeBlockFromCache();
  uint32_t FindSmallestFreeBlockExtent(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated);

  // Just mark the blocks as frees by adding them to the tree
  void AddFreeBlocks(uint32_t block_number, uint32_t blocks_count);

  // Remove blocks from the tree
  void RemoveFreeBlocksExtent(FreeBlocksExtentInfo info);

  std::shared_ptr<MetadataBlock> LoadAllocatorBlock(uint32_t block_number, bool new_block = false);

  std::shared_ptr<MetadataBlock> root_block() const { return block_; }

 private:
  std::unique_ptr<EPTree> GetEPTree();
  const FreeBlocksAllocatorHeader* header();
  FreeBlocksAllocatorHeader* mutable_header();

  std::shared_ptr<Area> area_;
  std::shared_ptr<MetadataBlock> block_;
};
