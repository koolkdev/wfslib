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
  TestFreeBlocksAllocator(std::shared_ptr<MetadataBlock> block, std::shared_ptr<TestBlocksDevice> device);

  void Init(int blocks);

  std::shared_ptr<MetadataBlock> LoadAllocatorBlock(uint32_t block_number, bool new_block = false) override;

 private:
  std::shared_ptr<TestBlocksDevice> blocks_device_;
};
