/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "test_free_blocks_allocator.h"

#include "../src/structs.h"
#include "test_blocks_device.h"
#include "test_metadata_block.h"

TestFreeBlocksAllocator::TestFreeBlocksAllocator(std::shared_ptr<MetadataBlock> block,
                                                 std::shared_ptr<TestBlocksDevice> device)
    : FreeBlocksAllocator(nullptr, std::move(block)), blocks_device_(device) {}

void TestFreeBlocksAllocator::Init(int blocks) {
  mutable_header()->free_metadata_block = 1;
  mutable_header()->free_metadata_blocks_count = blocks;
  mutable_header()->free_blocks_count = blocks;
}

std::shared_ptr<MetadataBlock> TestFreeBlocksAllocator::LoadAllocatorBlock(uint32_t block_number, bool new_block) {
  return TestMetadataBlock::LoadBlock(blocks_device_, block_number, new_block);
}
