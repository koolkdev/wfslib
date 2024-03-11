/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>

class EPTree;
class Area;
class MetadataBlock;
struct FreeBlocksAllocatorHeader;

class FreeBlocksAllocator {
 public:
  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block);

  uint32_t AllocMetedataBlockFromCache();
  uint32_t FindFreeMetadataBlockNumber(uint32_t near);

  std::shared_ptr<MetadataBlock> LoadAllocatorBlock(uint32_t block_number, bool new_block = false);

 private:
  std::unique_ptr<EPTree> GetEPTree();
  const FreeBlocksAllocatorHeader* header();
  FreeBlocksAllocatorHeader* mutable_header();

  std::shared_ptr<Area> area_;
  std::shared_ptr<MetadataBlock> block_;
};
