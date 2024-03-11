/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_allocator.h"

#include "area.h"
#include "free_blocks_allocator_tree.h"
#include "structs.h"

FreeBlocksAllocator::FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block)
    : area_(std::move(area)), block_(std::move(block)) {}

uint32_t FreeBlocksAllocator::AllocMetedataBlockFromCache() {
  if (header()->free_metadata_blocks_count.value() == 0)
    return 0;
  auto* header = mutable_header();
  auto res = header->free_metadata_block.value();
  header->free_metadata_block++;
  header->free_metadata_blocks_count--;
  header->free_blocks_count--;
  return res;
}

uint32_t FreeBlocksAllocator::FindFreeMetadataBlockNumber(uint32_t near) {
  for (size_t i = 0; i < kSizeBucketsCount; ++i) {
    FreeBlocksAllocatorBucket bucket(this, i);
    auto it = bucket.find(near);
    // Check if found
    if (it != bucket.end())
      return (*it).key;
  }
  // Not found
  return 0;
}

std::shared_ptr<MetadataBlock> FreeBlocksAllocator::LoadAllocatorBlock(uint32_t block_number, bool new_block) {
  return throw_if_error(area_->GetMetadataBlock(block_number, new_block));
}

const FreeBlocksAllocatorHeader* FreeBlocksAllocator::header() {
  return block_->get_object<FreeBlocksAllocatorHeader>(sizeof(MetadataBlockHeader));
}
FreeBlocksAllocatorHeader* FreeBlocksAllocator::mutable_header() {
  return block_->get_mutable_object<FreeBlocksAllocatorHeader>(sizeof(MetadataBlockHeader));
}
