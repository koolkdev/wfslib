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

class SubBlockAllocator {
 public:
  SubBlockAllocator(const std::shared_ptr<MetadataBlock>& block) : block_(block) {}

  template <typename T>
  T* GetNode(uint16_t offset) {
    return reinterpret_cast<T*>(&block_->Data()[offset]);
  }

  template <typename T>
  T* GetRootNode() {
    return GetNode<T>(Header()->root.value());
  }

  void Init();

  uint16_t Alloc(uint16_t size);
  void Free(uint16_t offset, uint16_t size);

 private:
  std::shared_ptr<MetadataBlock> block_;

  static const int MIN_BLOCK_SIZE = 3;  // 1 << 3
  static constexpr int MAX_BLOCK_SIZE =
      MIN_BLOCK_SIZE + std::extent<decltype(SubBlockAllocatorStruct::free_list)>::value - 1;

  SubBlockAllocatorStruct* Header();

  uint16_t PopFreeEntry(int size_index);

  void Unlink(SubBlockAllocatorFreeListEntry* entry, int size_index);
};
