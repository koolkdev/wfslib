/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "block.h"

// Log2 of number of block in for each single quanta in each bucket
// The first three buckets represent the tree possible blocks allocation sizes (single, large, and large cluster), and
// the rest are increasing by 16 every time. So the actual sizes are {0, 3, 6, 10, 14, 18, 22}
constexpr auto kSizeBuckets =
    std::to_array<int>({Block::BlockSizeType::Single, Block::BlockSizeType::Large, Block::BlockSizeType::LargeCluster,
                        Block::BlockSizeType::LargeCluster + 1 * 4, Block::BlockSizeType::LargeCluster + 2 * 4,
                        Block::BlockSizeType::LargeCluster + 3 * 4, Block::BlockSizeType::LargeCluster + 4 * 4});

class EPTree;
class Area;
class Block;
struct FreeBlocksAllocatorHeader;

struct FreeBlocksExtentInfo {
  uint32_t block_number;
  uint32_t blocks_count;
  size_t bucket_index;

  uint32_t end_block_number() const { return block_number + blocks_count; }

  bool operator==(const FreeBlocksExtentInfo& other) const {
    return block_number == other.block_number && blocks_count == other.blocks_count &&
           bucket_index == other.bucket_index;
  }
};

struct FreeBlocksRangeInfo {
  uint32_t block_number;
  uint32_t blocks_count;

  uint32_t end_block_number() const { return block_number + blocks_count; }
};

class FreeBlocksAllocator {
 public:
  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<Block> block);
  virtual ~FreeBlocksAllocator() = default;

  void Init(std::vector<FreeBlocksRangeInfo> initial_free_blocks);

  uint32_t AllocFreeBlockFromCache();
  uint32_t FindSmallestFreeBlockExtent(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated);

  std::optional<std::vector<uint32_t>> AllocBlocks(uint32_t chunks_count, Block::BlockSizeType size, bool use_cache);
  std::optional<std::vector<FreeBlocksRangeInfo>> AllocAreaBlocks(uint32_t chunks_count, Block::BlockSizeType size);

  // Mark the blocks as frees by adding them to the tree
  bool AddFreeBlocks(FreeBlocksRangeInfo range);

  // Remove blocks from the tree
  bool RemoveFreeBlocksExtent(FreeBlocksExtentInfo info);
  bool RemoveSpecificFreeBlocksExtent(FreeBlocksExtentInfo info);

  // Return whether any part of the block is freed
  bool IsRangeIsFree(FreeBlocksRangeInfo range);

  virtual std::shared_ptr<Block> LoadAllocatorBlock(uint32_t block_number, bool new_block = false);

  std::shared_ptr<Block> root_block() const { return block_; }

 protected:
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

  virtual size_t BlocksCacheSizeLog2() const;

  std::unique_ptr<EPTree> GetEPTree();
  const FreeBlocksAllocatorHeader* header() const;
  FreeBlocksAllocatorHeader* mutable_header();

 private:
  std::shared_ptr<Area> area_;
  std::shared_ptr<Block> block_;
};
