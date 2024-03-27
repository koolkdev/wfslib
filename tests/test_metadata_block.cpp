/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "test_metadata_block.h"

#include <wfslib/blocks_device.h>

TestMetadataBlock::TestMetadataBlock(std::shared_ptr<BlocksDevice> device, uint32_t block_number)
    : MetadataBlock(std::move(device), block_number, Block::BlockSize::Regular, 0) {}

TestMetadataBlock::~TestMetadataBlock() = default;

// static
std::shared_ptr<TestMetadataBlock> TestMetadataBlock::LoadBlock(std::shared_ptr<BlocksDevice> device,
                                                                uint32_t block_number,
                                                                bool new_block) {
  auto cached_block = device->GetFromCache(block_number);
  if (cached_block) {
    return std::dynamic_pointer_cast<TestMetadataBlock>(cached_block);
  }
  auto block = std::make_shared<TestMetadataBlock>(device, block_number);
  device->AddToCache(block_number, block);
  if (!new_block)
    block->Fetch();
  return block;
}
