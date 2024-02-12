/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include "metadata_block.h"
#include "structs.h"

class MetadataBlock;
struct SubBlockAllocatorStruct;

class SubBlockAllocatorBase {
 public:
  static const int BLOCK_SIZE_QUANTA = 3;  // 1 << 3
  static constexpr int MAX_BLOCK_SIZE =
      BLOCK_SIZE_QUANTA + std::extent<decltype(SubBlockAllocatorStruct::free_list)>::value - 1;

  SubBlockAllocatorBase(const std::shared_ptr<MetadataBlock>& block) : block_(block) {}

  uint16_t Alloc(uint16_t size);
  void Free(uint16_t offset, uint16_t size);

 protected:
  void Init(uint16_t extra_header_size);

  uint16_t header_offset() const { return sizeof(MetadataBlockHeader); }

  MetadataBlock* block() { return block_.get(); }
  const MetadataBlock* block() const { return block_.get(); }

 private:
  SubBlockAllocatorStruct* header() { return block()->GetStruct<SubBlockAllocatorStruct>(header_offset()); }
  const SubBlockAllocatorStruct* header() const { return block()->GetStruct<SubBlockAllocatorStruct>(header_offset()); }

  std::shared_ptr<MetadataBlock> block_;

  uint16_t PopFreeEntry(int size_index);
  void Unlink(SubBlockAllocatorFreeListEntry* entry, int size_index);
};

template <typename ExtraHeaderType>
class SubBlockAllocator : SubBlockAllocatorBase {
 public:
  SubBlockAllocator(const std::shared_ptr<MetadataBlock>& block) : SubBlockAllocatorBase(block) {}

  void Init() { Init(sizeof(ExtraHeaderType)); }

  ExtraHeaderType* extra_header() { return block()->GetStruct<ExtraHeaderType>(header_offset()); }
  const ExtraHeaderType* extra_header() const { return block()->GetStruct<ExtraHeaderType>(extra_header_offset()); }

 private:
  uint16_t extra_header_offset() const { return header_offset() + sizeof(SubBlockAllocatorStruct); }
};
