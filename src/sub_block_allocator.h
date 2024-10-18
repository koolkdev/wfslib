/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include "block.h"
#include "structs.h"

struct SubBlockAllocatorStruct;

class SubBlockAllocatorBase {
 public:
  static constexpr int BLOCK_SIZE_QUANTA = 3;  // 1 << 3
  static constexpr int MAX_BLOCK_SIZE =
      BLOCK_SIZE_QUANTA + std::extent<decltype(SubBlockAllocatorStruct::free_list)>::value - 1;

  SubBlockAllocatorBase() = default;
  SubBlockAllocatorBase(std::shared_ptr<Block> block) : block_(std::move(block)) {}

  uint16_t Alloc(uint16_t size);
  void Free(uint16_t offset, uint16_t size);
  void Shrink(uint16_t offset, uint16_t old_size, uint16_t new_size);

  const std::shared_ptr<Block>& block() const { return block_; }

 protected:
  void Init(uint16_t extra_header_size);

  uint16_t header_offset() const { return sizeof(MetadataBlockHeader); }

 private:
  SubBlockAllocatorStruct* mutable_header() {
    return block()->get_mutable_object<SubBlockAllocatorStruct>(header_offset());
  }
  const SubBlockAllocatorStruct* header() const {
    return block()->get_object<SubBlockAllocatorStruct>(header_offset());
  }

  std::shared_ptr<Block> block_;

  uint16_t PopFreeEntry(int size_index);
  void Unlink(const SubBlockAllocatorFreeListEntry* entry, int size_index);
};

template <typename ExtraHeaderType>
class SubBlockAllocator : public SubBlockAllocatorBase {
 public:
  SubBlockAllocator() = default;
  SubBlockAllocator(const std::shared_ptr<Block>& block) : SubBlockAllocatorBase(block) {}

  void Init() { SubBlockAllocatorBase::Init(sizeof(ExtraHeaderType)); }

  ExtraHeaderType* mutable_extra_header() {
    return block()->template get_mutable_object<ExtraHeaderType>(extra_header_offset());
  }
  const ExtraHeaderType* extra_header() const {
    return block()->template get_object<ExtraHeaderType>(extra_header_offset());
  }

 private:
  uint16_t extra_header_offset() const { return header_offset() + sizeof(SubBlockAllocatorStruct); }
};
