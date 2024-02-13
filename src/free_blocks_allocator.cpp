/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_allocator.h"

FreeBlocksAllocator::FreeBlocksAllocator(std::shared_ptr<const Area> area, std::shared_ptr<MetadataBlock> root_block)
    : area_(area), tree_{Adapter{area}, root_block->BlockNumber()}, root_block_(std::move(root_block)) {}
