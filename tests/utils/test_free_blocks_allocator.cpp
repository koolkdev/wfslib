/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "test_free_blocks_allocator.h"

#include "eptree.h"
#include "ftree.h"
#include "structs.h"
#include "test_area.h"
#include "test_block.h"
#include "test_blocks_device.h"

TestFreeBlocksAllocator::TestFreeBlocksAllocator(std::shared_ptr<Block> block,
                                                 std::shared_ptr<TestBlocksDevice> device,
                                                 std::shared_ptr<TestArea> area)
    : FreeBlocksAllocator(area, std::move(block)), blocks_device_(device), area_(area) {}

bool TestFreeBlocksAllocator::Init(uint32_t free_cache_blocks, uint32_t free_tree_blocks) {
  if (area_) {
    area_->Init(free_cache_blocks + free_tree_blocks + 2);
  }
  initial_ftrees_block_number_ = root_block()->physical_block_number() + 1;
  initial_frees_block_number_ = initial_ftrees_block_number_ + 1;

  if (free_cache_blocks) {
    mutable_header()->free_blocks_cache = initial_frees_block_number_;
    mutable_header()->free_blocks_cache_count = free_cache_blocks;
    mutable_header()->free_blocks_count = free_cache_blocks;
  } else {
    mutable_header()->free_blocks_cache = 0;
    mutable_header()->free_blocks_cache_count = 0;
    mutable_header()->free_blocks_count = 0;
  }

  EPTree eptree{this};
  eptree.Init(/*block_number=*/0);
  auto initial_ftrees_block = LoadAllocatorBlock(initial_ftrees_block_number_, true);
  FTreesBlock{initial_ftrees_block}.Init();
  if (!eptree.insert({0, initial_ftrees_block_number_}))
    return false;

  if (free_tree_blocks) {
    return AddFreeBlocks({initial_frees_block_number_ + free_cache_blocks, free_tree_blocks});
  }
  return true;
}

std::shared_ptr<Block> TestFreeBlocksAllocator::LoadAllocatorBlock(uint32_t block_number, bool new_block) {
  return TestBlock::LoadMetadataBlock(blocks_device_, block_number, new_block);
}
