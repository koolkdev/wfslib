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

class Block;
struct SubBlockAllocatorStruct;

template <typename T, size_t N>
concept check_node_size = (sizeof(T) == N);

template <typename ExtraHeaderType, typename TreeHeaderType, std::size_t EntrySize>
struct TreeNodesAllocatorArgs {
  using extra_header_type = ExtraHeaderType;
  using tree_header_type = TreeHeaderType;
  constexpr static std::size_t entry_size = EntrySize;
};

template <typename T>
struct is_tree_nodes_allocator_args : std::false_type {};

template <typename extra_header_type, typename tree_header_type, std::size_t entry_size>
struct is_tree_nodes_allocator_args<TreeNodesAllocatorArgs<extra_header_type, tree_header_type, entry_size>>
    : std::true_type {};

template <typename T>
constexpr bool is_tree_nodes_allocator_args_v = is_tree_nodes_allocator_args<T>::value;

template <typename Args>
  requires is_tree_nodes_allocator_args_v<Args>
class TreeNodesAllocator {
 public:
  using extra_header_type = typename Args::extra_header_type;
  using tree_header_type = typename Args::tree_header_type;
  constexpr static std::size_t entry_size = Args::entry_size;

  static_assert(entry_size >= sizeof(HeapFreelistEntry));

  TreeNodesAllocator() = default;
  TreeNodesAllocator(const std::shared_ptr<Block>& block) : block_(block) {}

  void Init() {
    auto* header = mutable_heap_header();
    header->start_offset = header_size();
    header->freelist_head = 0;
    header->allocated_entries = 0;
    header->start_offset = header_size();
    header->total_bytes = initial_entries_total_bytes();
    auto* freelist_entry = get_mutable_entry<HeapFreelistEntry>(header->freelist_head.value());
    freelist_entry->count = entries_count();
    freelist_entry->init_zero = 0;
    freelist_entry->next = entries_count();
    std::fill(reinterpret_cast<std::byte*>(mutable_tree_header()),
              reinterpret_cast<std::byte*>(mutable_tree_header() + 1), std::byte{0});
  }

  template <typename T>
  T* Alloc(uint16_t count)
    requires check_node_size<T, entry_size>
  {
    // Find first entry with at least count frees.
    const HeapFreelistEntry* current = nullptr;
    uint32_t prev_index = 0;
    for (uint32_t current_index = heap_header()->freelist_head.value(); current_index < entries_count();
         current_index = current->next.value()) {
      current = get_freelist_entry(current_index);
      if (count <= current->count.value()) {
        if (count == current->count.value()) {
          if (current_index == heap_header()->freelist_head.value())
            mutable_heap_header()->freelist_head = static_cast<uint16_t>(current->next.value());
          else
            get_mutable_freelist_entry(prev_index)->next = current->next;
        } else {
          auto* new_free = get_mutable_freelist_entry(current_index + count);
          new_free->next = current->next.value();
          new_free->count = current->count.value() - count;
          if (current_index == heap_header()->freelist_head.value())
            mutable_heap_header()->freelist_head = static_cast<uint16_t>(current_index + count);
          else
            get_mutable_freelist_entry(prev_index)->next = current_index + count;
        }
        mutable_heap_header()->allocated_entries += count;
        return get_mutable_entry<T>(current_index);
      }
      prev_index = current_index;
    }
    return nullptr;
  }

  template <typename T>
  void Free(const T* node, uint16_t count)
    requires check_node_size<T, entry_size>
  {
    assert(count > 0 && node >= get_entry<T>(0) && (node + count - 1) <= get_entry<T>(entries_count() - 1));
    mutable_heap_header()->allocated_entries -= count;
    uint32_t index = static_cast<uint16_t>(node - get_entry<T>(0));
    uint32_t next_index;
    if (index < heap_header()->freelist_head.value()) {
      // our entry is before the freelist, so no prev
      next_index = heap_header()->freelist_head.value();
      mutable_heap_header()->freelist_head = static_cast<uint16_t>(index);
    } else {
      // try coallese with prev
      uint32_t prev_index;
      for (prev_index = heap_header()->freelist_head.value(); get_freelist_entry(prev_index)->next.value() < index;
           prev_index = get_freelist_entry(prev_index)->next.value())
        ;
      auto* prev = get_mutable_freelist_entry(prev_index);
      next_index = prev->next.value();
      if (prev_index + prev->count.value() == index) {
        count += prev->count.value();
        index = prev_index;
      } else {
        prev->next = index;
      }
    }

    auto* new_freelist_entry = get_mutable_freelist_entry(index);
    // try coallese with next if there is next
    if (next_index != entries_count() && next_index == index + count) {
      auto* next = get_freelist_entry(next_index);
      new_freelist_entry->next = next->next.value();
      new_freelist_entry->count = count + next->count.value();
    } else {
      new_freelist_entry->count = count;
      new_freelist_entry->next = next_index;
    }
  }

  template <typename T>
  uint16_t to_offset(const T* node) const
    requires check_node_size<T, entry_size>
  {
    return static_cast<uint16_t>(reinterpret_cast<const uint8_t*>(node) - block()->template get_object<uint8_t>(0));
  }

  extra_header_type* mutable_extra_header() {
    return block()->template get_mutable_object<extra_header_type>(extra_header_offset());
  }
  const extra_header_type* extra_header() const {
    return block()->template get_object<extra_header_type>(extra_header_offset());
  }

  std::shared_ptr<Block> block() const { return block_; }

  template <typename T>
  T* get_mutable_object(uint16_t offset) const
    requires check_node_size<T, entry_size>
  {
    return block_->template get_mutable_object<T>(offset);
  }
  template <typename T>
  const T* get_object(uint16_t offset) const
    requires check_node_size<T, entry_size>
  {
    return block_->template get_object<T>(offset);
  }

  tree_header_type* mutable_tree_header() {
    return block_->template get_mutable_object<tree_header_type>(tree_header_offset());
  }
  const tree_header_type* tree_header() const {
    return block_->template get_object<tree_header_type>(tree_header_offset());
  }

 protected:
  uint16_t initial_entries_total_bytes() const {
    return static_cast<uint16_t>(block_->size()) - footer_size() - header_size();
  }
  uint16_t initial_entries_count() const { return initial_entries_total_bytes() / entry_size; }
  uint16_t total_bytes() const { return heap_header()->total_bytes.value(); }
  uint16_t entries_count() const { return total_bytes() / entry_size; }
  uint16_t entries_start_offset() const { return heap_header()->start_offset.value(); }

  template <typename T>
  T* get_mutable_entry(uint32_t index) {
    assert(index < entries_count());
    return block_->template get_mutable_object<T>(entries_start_offset() + entry_size * index);
  }
  template <typename T>
  const T* get_entry(uint32_t index) const {
    assert(index < entries_count());
    return block_->template get_object<T>(entries_start_offset() + entry_size * index);
  }

  HeapFreelistEntry* get_mutable_freelist_entry(uint32_t index) { return get_mutable_entry<HeapFreelistEntry>(index); }
  const HeapFreelistEntry* get_freelist_entry(uint32_t index) const { return get_entry<HeapFreelistEntry>(index); }

  constexpr std::size_t get_entry_size() const { return entry_size; }
  constexpr uint16_t footer_size() const { return sizeof(tree_header_type) + sizeof(HeapHeader); }
  constexpr uint16_t header_size() const { return sizeof(MetadataBlockHeader) + sizeof(extra_header_type); }
  uint16_t footer_offset() const { return static_cast<uint16_t>(block_->size() - footer_size()); }
  constexpr uint16_t heap_header_offset() const { return footer_offset() + sizeof(tree_header_type); }
  constexpr uint16_t tree_header_offset() const { return footer_offset(); }
  constexpr uint16_t extra_header_offset() const { return sizeof(MetadataBlockHeader); }

  HeapHeader* mutable_heap_header() { return block_->template get_mutable_object<HeapHeader>(heap_header_offset()); }
  const HeapHeader* heap_header() const { return block_->template get_object<HeapHeader>(heap_header_offset()); }

  std::shared_ptr<Block> block_;
};
