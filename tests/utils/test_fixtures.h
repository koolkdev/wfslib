/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>

#include "test_area.h"
#include "test_block.h"
#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"

class MetadataBlockFixture {
 public:
  std::shared_ptr<TestBlock> LoadMetadataBlock(uint32_t physical_block_number, bool new_block = true) {
    return TestBlock::LoadMetadataBlock(test_device, physical_block_number, new_block);
  }

  std::shared_ptr<TestBlocksDevice> test_device = std::make_shared<TestBlocksDevice>();
};

class FreeBlocksAllocatorFixture : public MetadataBlockFixture {
 public:
  std::shared_ptr<TestBlock> allocator_block = LoadMetadataBlock(0);
  TestFreeBlocksAllocator allocator{allocator_block, test_device};
};

class AreaAllocatorFixture : public MetadataBlockFixture {
 public:
  std::shared_ptr<TestBlock> area_block = LoadMetadataBlock(0);
  std::shared_ptr<TestBlock> allocator_block = LoadMetadataBlock(1);
  std::shared_ptr<TestArea> area = std::make_shared<TestArea>(std::move(area_block));
  TestFreeBlocksAllocator allocator{allocator_block, test_device, area};
};
