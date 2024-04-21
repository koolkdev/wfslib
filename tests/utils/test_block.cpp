/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "test_block.h"

#include <wfslib/blocks_device.h>

TestBlock::TestBlock(std::shared_ptr<BlocksDevice> device, uint32_t block_number)
    : Block(std::move(device),
            block_number,
            Block::BlockSize::Regular,
            1 << Block::BlockSize::Regular,
            /*iv=*/0,
            /*hash_ref=*/{},
            /*encrypted=*/false) {}

TestBlock::~TestBlock() = default;

// static
std::shared_ptr<TestBlock> TestBlock::LoadMetadataBlock(std::shared_ptr<BlocksDevice> device,
                                                        uint32_t block_number,
                                                        bool new_block) {
  auto cached_block = device->GetFromCache(block_number);
  if (cached_block) {
    return std::dynamic_pointer_cast<TestBlock>(cached_block);
  }
  auto block = std::make_shared<TestBlock>(device, block_number);
  device->AddToCache(block_number, block);
  if (!new_block)
    block->Fetch();
  return block;
}
