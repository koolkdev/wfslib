/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_allocator.h"

FreeBlocksAllocator::FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> root_block)
    : area_(area), tree_(area, root_block), root_block_(std::move(root_block)) {}

void FreeBlocksAllocator::iterate() const {
  for (const auto& [tree_block_number, free_tree] : tree()) {
    for (const auto& free_tree_per_size : free_tree) {
      for (const auto& [block_number, blocks_count] : free_tree_per_size) {
        std::ignore = block_number;
        std::ignore = blocks_count;
      }
    }
  }
}
