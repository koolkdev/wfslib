/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "area.h"
#include "free_blocks_allocator_tree.h"

class MetadataBlock;

class FreeBlocksAllocator {
 public:
  class Adapter {
   public:
    Adapter(std::shared_ptr<const Area> area) : area_(area) {}
    MetadataBlock::Adapter get_block(int32_t block_number) const { return {area_->GetMetadataBlock(block_number)}; }

   private:
    std::shared_ptr<const Area> area_;
  };

  using Tree = EPTree<FreeBlocksAllocator::Adapter, MetadataBlock::Adapter>;

  FreeBlocksAllocator(std::shared_ptr<const Area> area, uint32_t block_number);

  FreeBlocksAllocatorHeader* header() { return root_block()->get_object<FreeBlocksAllocatorHeader>(header_offset()); }
  const FreeBlocksAllocatorHeader* header() const {
    return root_block()->get_object<FreeBlocksAllocatorHeader>(header_offset());
  }

  Tree& tree() { return tree_; }
  const Tree& tree() const { return tree_; }

 private:
  MetadataBlock* root_block() { return root_block_.get(); }
  const MetadataBlock* root_block() const { return root_block_.get(); }

  uint16_t header_offset() const { return sizeof(MetadataBlockHeader); }

  std::shared_ptr<const Area> area_;
  Tree tree_;

  std::shared_ptr<MetadataBlock> root_block_;
};
