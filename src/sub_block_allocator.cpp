/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <algorithm>
#include <bit>
#include <cassert>
#include <optional>

#include "sub_block_allocator.h"

namespace {

int GetSizeGroup(uint16_t size) {
  assert(size > 0);
  int size_log2 = std::bit_width(static_cast<uint16_t>(size - 1));
  size_log2 = std::max(size_log2, SubBlockAllocatorBase::BLOCK_SIZE_QUANTA);
  assert(size_log2 <= SubBlockAllocatorBase::MAX_BLOCK_SIZE);
  return size_log2;
}

}  // namespace

void SubBlockAllocatorBase::Init(uint16_t extra_header_size) {
  auto* header = mutable_header();
  uint16_t total_headers_size = sizeof(MetadataBlockHeader) + sizeof(*header) + extra_header_size;
  std::fill(block()->mutable_data().begin(), block()->mutable_data().begin() + total_headers_size, std::byte{0});

  uint16_t free_entries = 1 << (block_->log2_size() - MAX_BLOCK_SIZE);
  for (uint16_t i = 0; i < free_entries; ++i) {
    auto* entry = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(i << MAX_BLOCK_SIZE);
    entry->next = ((i + 1) & (free_entries - 1)) << MAX_BLOCK_SIZE;
    entry->prev = ((i - 1) & (free_entries - 1)) << MAX_BLOCK_SIZE;
    entry->log2_block_size = MAX_BLOCK_SIZE;
  }
  header->free_list[MAX_BLOCK_SIZE - BLOCK_SIZE_QUANTA].free_blocks_count = free_entries;

  // Reserve header
  [[maybe_unused]] auto res = Alloc(total_headers_size);
  assert(res.has_value() && *res == 0);
}

std::optional<uint16_t> SubBlockAllocatorBase::Alloc(uint16_t size) {
  int size_log2 = GetSizeGroup(size);
  auto offset = PopFreeEntry(size_log2 - BLOCK_SIZE_QUANTA);
  if (offset.has_value()) {
    return offset;
  }
  int base_size_log2 = size_log2;
  for (; !offset.has_value(); offset = PopFreeEntry(size_log2 - BLOCK_SIZE_QUANTA)) {
    if (++size_log2 > MAX_BLOCK_SIZE)
      return std::nullopt;
  }

  // Split a bigger block
  for (uint16_t sub_block_offset = *offset + (1 << base_size_log2); base_size_log2 < size_log2;
       sub_block_offset += (1 << (base_size_log2++))) {
    auto* free_entry = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(sub_block_offset);
    free_entry->free_mark = free_entry->FREE_MARK_CONST;
    free_entry->prev = sub_block_offset;
    free_entry->next = sub_block_offset;
    free_entry->log2_block_size = static_cast<uint16_t>(base_size_log2);
    mutable_header()->free_list[base_size_log2 - BLOCK_SIZE_QUANTA].head = sub_block_offset;
    mutable_header()->free_list[base_size_log2 - BLOCK_SIZE_QUANTA].free_blocks_count = 1;
  }

  return offset;
}

bool SubBlockAllocatorBase::CanAlloc(uint16_t size) const {
  for (int size_log2 = GetSizeGroup(size); size_log2 <= MAX_BLOCK_SIZE; ++size_log2) {
    if (header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].free_blocks_count.value())
      return true;
  }
  return false;
}

void SubBlockAllocatorBase::Free(uint16_t offset, uint16_t size) {
  int size_log2 = GetSizeGroup(size);
  assert(!(offset & ((1 << size_log2) - 1)));
  for (; size_log2 < MAX_BLOCK_SIZE; size_log2++) {
    // Try to coalesce if block not max size
    auto* other_free_block = block()->get_object<SubBlockAllocatorFreeListEntry>(offset ^ (1 << size_log2));
    if (other_free_block->free_mark.value() != other_free_block->FREE_MARK_CONST ||
        other_free_block->log2_block_size.value() != size_log2) {
      // Other half not free
      break;
    }
    // Coalesce with other free block
    Unlink(other_free_block, size_log2 - BLOCK_SIZE_QUANTA);
    offset = std::min(offset, (uint16_t)(offset ^ (1 << size_log2)));
  }

  auto* free_entry = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(offset);
  auto free_blocks_count = header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].free_blocks_count.value();
  if (free_blocks_count) {
    // Insert at end of list
    uint16_t head_offset = header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].head.value();
    mutable_header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].free_blocks_count++;
    auto* next_free = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(head_offset);
    auto* prev_free = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(next_free->prev.value());
    free_entry->next = head_offset;
    free_entry->prev = next_free->prev.value();
    next_free->prev = offset;
    prev_free->next = offset;

  } else {
    // New list
    mutable_header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].head = offset;
    mutable_header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].free_blocks_count = 1;
    free_entry->next = offset;
    free_entry->prev = offset;
  }
  free_entry->log2_block_size = static_cast<uint16_t>(size_log2);
  free_entry->free_mark = free_entry->FREE_MARK_CONST;
}

void SubBlockAllocatorBase::Shrink(uint16_t offset, uint16_t old_size, uint16_t new_size) {
  assert(new_size < old_size);
  int old_size_log2 = GetSizeGroup(old_size);
  int new_size_log2 = GetSizeGroup(new_size);
  uint16_t free_offset = offset + (1 << new_size_log2);
  while (free_offset < offset + (1 << old_size_log2)) {
    Free(free_offset, 1 << new_size_log2);
    free_offset += 1 << new_size_log2++;
  }
  assert(free_offset == offset + (1 << old_size_log2));
}

uint16_t SubBlockAllocatorBase::GetFreeBytes() const {
  uint16_t sum = 0;
  for (int size_log2 = BLOCK_SIZE_QUANTA; size_log2 <= MAX_BLOCK_SIZE; ++size_log2)
    sum += header()->free_list[size_log2 - BLOCK_SIZE_QUANTA].free_blocks_count.value() << size_log2;
  return sum;
}

void SubBlockAllocatorBase::Unlink(const SubBlockAllocatorFreeListEntry* entry, int size_index) {
  auto* prev_free = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(entry->prev.value());
  auto* next_free = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(entry->next.value());
  next_free->prev = entry->prev.value();
  prev_free->next = entry->next.value();
  mutable_header()->free_list[size_index].head = entry->next.value();
  mutable_header()->free_list[size_index].free_blocks_count =
      header()->free_list[size_index].free_blocks_count.value() - 1;
}

std::optional<uint16_t> SubBlockAllocatorBase::PopFreeEntry(int size_index) {
  auto free_blocks_count = header()->free_list[size_index].free_blocks_count.value();
  if (free_blocks_count == 0)
    return std::nullopt;
  uint16_t entry_offset = header()->free_list[size_index].head.value();
  auto* free_entry = block()->get_mutable_object<SubBlockAllocatorFreeListEntry>(entry_offset);
  free_entry->free_mark = 0;
  Unlink(free_entry, size_index);
  return entry_offset;
}
