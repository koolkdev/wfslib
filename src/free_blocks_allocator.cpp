/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_allocator.h"

FreeBlocksAllocator::FreeBlocksAllocator(std::shared_ptr<const Area> area, uint32_t block_number)
    : area_(area), tree_{Adapter{area}, block_number} {
  root_block_ = area_->GetMetadataBlock(block_number);
}
