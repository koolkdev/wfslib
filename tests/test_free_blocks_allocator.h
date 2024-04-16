/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "../src/free_blocks_allocator.h"

class TestBlocksDevice;

class TestFreeBlocksAllocator : public FreeBlocksAllocator {
 public:
  TestFreeBlocksAllocator(std::shared_ptr<Block> block, std::shared_ptr<TestBlocksDevice> device);
  ~TestFreeBlocksAllocator() override = default;

  bool Init(uint32_t free_cache_blocks, uint32_t free_tree_blocks = 0);

  std::shared_ptr<Block> LoadAllocatorBlock(uint32_t block_number, bool new_block = false) override;

  uint32_t initial_ftrees_block_number() const { return initial_ftrees_block_number_; }
  uint32_t initial_frees_block_number() const { return initial_frees_block_number_; }

 private:
  std::shared_ptr<TestBlocksDevice> blocks_device_;

  uint32_t initial_ftrees_block_number_{0};
  uint32_t initial_frees_block_number_{0};
};
