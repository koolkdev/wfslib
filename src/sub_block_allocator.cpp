/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "sub_block_allocator.h"
#include <bit>
#include <cassert>

namespace {

int GetSizeGroup(uint16_t size) {
  assert(size > 0);
  int size_log2 = std::bit_width(static_cast<uint16_t>(size - 1));
  size_log2 = std::min(size_log2, SubBlockAllocator::MIN_BLOCK_SIZE);
  assert(size_log2 <= MAX_BLOCK_SIZE);
  return size_log2;
}

}  // namespace

SubBlockAllocatorStruct* SubBlockAllocator::Header() {
  return GetNode<SubBlockAllocatorStruct>(sizeof(MetadataBlockHeader));
}

void SubBlockAllocator::Init() {
  auto* header = Header();
  memset(header, 0, sizeof(*header));

  uint16_t free_entries = 1 << (block_->Size() - MAX_BLOCK_SIZE);
  for (uint16_t i = 0; i < free_entries; ++i) {
    auto* entry = GetNode<SubBlockAllocatorFreeListEntry>(i << MAX_BLOCK_SIZE);
    entry->next = ((i + 1) & (free_entries - 1)) << MAX_BLOCK_SIZE;
    entry->prev = ((i - 1) & (free_entries - 1)) << MAX_BLOCK_SIZE;
    entry->log2_block_size = MAX_BLOCK_SIZE;
  }
  header->free_list[MAX_BLOCK_SIZE - MIN_BLOCK_SIZE].free_blocks_count = free_entries;

  // Reserve header
  Alloc(sizeof(MetadataBlockHeader) + sizeof(*header));
}

uint16_t SubBlockAllocator::Alloc(uint16_t size) {
  int size_log2 = GetSizeGroup(size);
  uint16_t offset = PopFreeEntry(size_log2 - MIN_BLOCK_SIZE);
  if (offset) {
    return offset;
  }
  int base_size_log2 = size_log2++;
  for (; !offset; offset = PopFreeEntry(size_log2 - MIN_BLOCK_SIZE)) {
    if (++size_log2 > MAX_BLOCK_SIZE - MIN_BLOCK_SIZE)
      return 0;
  }

  // Split a bigger block
  for (uint16_t sub_block_offset = offset + (1 << base_size_log2); base_size_log2 < size_log2;
       sub_block_offset += (1 << (base_size_log2++))) {
    auto* free_entry = GetNode<SubBlockAllocatorFreeListEntry>(sub_block_offset);
    free_entry->free_mark = free_entry->FREE_MARK_CONST;
    free_entry->prev = sub_block_offset;
    free_entry->next = sub_block_offset;
    free_entry->log2_block_size = static_cast<uint16_t>(base_size_log2);
    Header()->free_list[base_size_log2 - MIN_BLOCK_SIZE].head = sub_block_offset;
    Header()->free_list[base_size_log2 - MIN_BLOCK_SIZE].free_blocks_count = 1;
  }

  return offset;
}

void SubBlockAllocator::Free(uint16_t offset, uint16_t size) {
  int size_log2 = GetSizeGroup(size);
  assert(!(offset & ((1 << size_log2) - 1)));
  for (; size_log2 < MAX_BLOCK_SIZE; size_log2++) {
    // Try to coalesce if block not max size
    auto* other_free_block = GetNode<SubBlockAllocatorFreeListEntry>(offset ^ (1 << size_log2));
    if (other_free_block->free_mark.value() != other_free_block->FREE_MARK_CONST ||
        other_free_block->log2_block_size.value() != size_log2) {
      // Other half not free
      break;
    }
    // Coalesce with other free block
    Unlink(other_free_block, size_log2 - MIN_BLOCK_SIZE);
    offset = std::min(offset, (uint16_t)(offset ^ (1 << size_log2)));
  }

  auto* free_entry = GetNode<SubBlockAllocatorFreeListEntry>(offset);
  auto free_blocks_count = Header()->free_list[size_log2 - MIN_BLOCK_SIZE].free_blocks_count.value();
  if (free_blocks_count) {
    // Insert at end of list
    uint16_t head_offset = Header()->free_list[size_log2 - MIN_BLOCK_SIZE].head.value();
    auto* next_free = GetNode<SubBlockAllocatorFreeListEntry>(head_offset);
    auto* prev_free = GetNode<SubBlockAllocatorFreeListEntry>(next_free->prev.value());
    free_entry->next = head_offset;
    free_entry->prev = next_free->prev.value();
    next_free->prev = offset;
    prev_free->next = offset;

  } else {
    // New list
    Header()->free_list[size_log2 - MIN_BLOCK_SIZE].head = offset;
    Header()->free_list[size_log2 - MIN_BLOCK_SIZE].free_blocks_count = 1;
    free_entry->next = offset;
    free_entry->prev = offset;
  }
  free_entry->log2_block_size = static_cast<uint16_t>(size_log2);
  free_entry->free_mark = free_entry->FREE_MARK_CONST;
}

void SubBlockAllocator::Unlink(SubBlockAllocatorFreeListEntry* entry, int size_index) {
  auto* prev_free = GetNode<SubBlockAllocatorFreeListEntry>(entry->prev.value());
  auto* next_free = GetNode<SubBlockAllocatorFreeListEntry>(entry->next.value());
  next_free->prev = entry->prev.value();
  prev_free->next = entry->next.value();
  Header()->free_list[size_index].head = entry->next.value();
  Header()->free_list[size_index].free_blocks_count = Header()->free_list[size_index].free_blocks_count.value() - 1;
}

uint16_t SubBlockAllocator::PopFreeEntry(int size_index) {
  auto free_blocks_count = Header()->free_list[size_index].free_blocks_count.value();
  if (free_blocks_count == 0)
    return 0;
  uint16_t entry_offset = Header()->free_list[size_index].head.value();
  auto* free_entry = GetNode<SubBlockAllocatorFreeListEntry>(entry_offset);
  free_entry->free_mark = 0;
  Unlink(free_entry, size_index);
  return entry_offset;
}
