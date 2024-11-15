/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include "../src/tree_nodes_allocator.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"

namespace {

struct DummyExtraHeader {
  uint32_be_t a, b;
};

struct DummyTreeHeader {
  uint32_be_t a, b;
};

struct DummyEntry {
  uint32_be_t a, b, c, d;
};

}  // namespace

using DummyAllocatorBlockArgs = TreeNodesAllocatorArgs<DummyExtraHeader, DummyExtraHeader, sizeof(DummyEntry)>;

class DummyAllocatorBlock : public TreeNodesAllocator<DummyAllocatorBlockArgs> {
 public:
  using base = TreeNodesAllocator<DummyAllocatorBlockArgs>;

  DummyAllocatorBlock(std::shared_ptr<Block> block) : base(std::move(block)) {}

  const HeapHeader* get_heap_header() const { return base::heap_header(); }
  const HeapFreelistEntry* get_freelist_entry(uint32_t index) const { return base::get_freelist_entry(index); }
};

TEST_CASE("TreeNodesAllocatorTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto block = TestBlock::LoadMetadataBlock(test_device, 0);
  DummyAllocatorBlock allocator{block};
  allocator.Init();

  constexpr size_t total_bytes = (size_t{1} << log2_size(BlockSize::Logical)) - sizeof(MetadataBlockHeader) -
                                 sizeof(DummyExtraHeader) - sizeof(DummyTreeHeader) - sizeof(HeapHeader);
  constexpr size_t max_entries_count = total_bytes / 0x10;
  constexpr size_t offset = sizeof(MetadataBlockHeader) + sizeof(DummyExtraHeader);

  SECTION("Check initial heap state") {
    auto* heap_header = allocator.get_heap_header();
    REQUIRE(heap_header->freelist_head.value() == 0);
    REQUIRE(heap_header->allocated_entries.value() == 0);
    REQUIRE(heap_header->total_bytes.value() == total_bytes);
    REQUIRE(heap_header->start_offset.value() == offset);

    auto freelist_entry = allocator.get_freelist_entry(0);
    REQUIRE(freelist_entry->count.value() == max_entries_count);
    REQUIRE(freelist_entry->next.value() == max_entries_count);
  }

  SECTION("Alloc and free one") {
    auto* entry = allocator.Alloc<DummyEntry>(1);
    REQUIRE(entry == block->get_object<DummyEntry>(offset));
    REQUIRE(allocator.to_offset(entry) == offset);

    auto* heap_header = allocator.get_heap_header();
    REQUIRE(heap_header->freelist_head.value() == 1);
    REQUIRE(heap_header->allocated_entries.value() == 1);
    REQUIRE(heap_header->total_bytes.value() == total_bytes);
    REQUIRE(heap_header->start_offset.value() == offset);

    auto freelist_entry1 = allocator.get_freelist_entry(1);
    REQUIRE(freelist_entry1->count.value() == max_entries_count - 1);
    REQUIRE(freelist_entry1->next.value() == max_entries_count);

    allocator.Free(entry, 1);
    REQUIRE(heap_header->freelist_head.value() == 0);
    REQUIRE(heap_header->allocated_entries.value() == 0);

    auto freelist_entry0 = allocator.get_freelist_entry(0);
    REQUIRE(freelist_entry0->count.value() == max_entries_count);
    REQUIRE(freelist_entry0->next.value() == max_entries_count);
  }

  SECTION("Alloc and free multiple") {
    auto* entry1 = allocator.Alloc<DummyEntry>(1);
    REQUIRE(allocator.to_offset(entry1) == offset);
    auto* entry2 = allocator.Alloc<DummyEntry>(2);
    REQUIRE(entry2 == entry1 + 1);
    auto* entry3 = allocator.Alloc<DummyEntry>(1);
    REQUIRE(entry3 == entry2 + 2);

    auto* heap_header = allocator.get_heap_header();
    REQUIRE(heap_header->freelist_head.value() == 4);
    REQUIRE(heap_header->allocated_entries.value() == 4);
    REQUIRE(heap_header->total_bytes.value() == total_bytes);
    REQUIRE(heap_header->start_offset.value() == offset);
    auto freelist_entry4 = allocator.get_freelist_entry(4);
    REQUIRE(freelist_entry4->count.value() == max_entries_count - 4);
    REQUIRE(freelist_entry4->next.value() == max_entries_count);

    allocator.Free(entry2, 2);
    REQUIRE(heap_header->freelist_head.value() == 1);
    REQUIRE(heap_header->allocated_entries.value() == 2);
    auto freelist_entry1 = allocator.get_freelist_entry(1);
    REQUIRE(freelist_entry1->count.value() == 2);
    REQUIRE(freelist_entry1->next.value() == 4);
    REQUIRE(freelist_entry4->count.value() == max_entries_count - 4);
    REQUIRE(freelist_entry4->next.value() == max_entries_count);

    allocator.Free(entry1, 1);
    REQUIRE(heap_header->freelist_head.value() == 0);
    REQUIRE(heap_header->allocated_entries.value() == 1);
    auto freelist_entry0 = allocator.get_freelist_entry(0);
    REQUIRE(freelist_entry0->count.value() == 3);
    REQUIRE(freelist_entry0->next.value() == 4);
    REQUIRE(freelist_entry4->count.value() == max_entries_count - 4);
    REQUIRE(freelist_entry4->next.value() == max_entries_count);

    allocator.Free(entry3, 1);
    REQUIRE(heap_header->freelist_head.value() == 0);
    REQUIRE(heap_header->allocated_entries.value() == 0);
    REQUIRE(freelist_entry0->count.value() == max_entries_count);
    REQUIRE(freelist_entry0->next.value() == max_entries_count);
  }

  SECTION("Fill heap") {
    auto* entry = allocator.Alloc<DummyEntry>(max_entries_count);
    REQUIRE(allocator.to_offset(entry) == offset);
    auto* entry2 = allocator.Alloc<DummyEntry>(1);
    REQUIRE(entry2 == nullptr);
    auto* heap_header = allocator.get_heap_header();
    REQUIRE(heap_header->freelist_head.value() == max_entries_count);
    REQUIRE(heap_header->allocated_entries.value() == max_entries_count);

    allocator.Free(entry, max_entries_count);
    REQUIRE(heap_header->freelist_head.value() == 0);
    REQUIRE(heap_header->allocated_entries.value() == 0);
  }

  SECTION("Alloc too much") {
    auto* entry = allocator.Alloc<DummyEntry>(max_entries_count + 1);
    REQUIRE(entry == nullptr);
  }
}
