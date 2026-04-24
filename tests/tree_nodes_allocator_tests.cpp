/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include "tree_nodes_allocator.h"

#include "utils/test_fixtures.h"

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

using DummyAllocatorBlockArgs = TreeNodesAllocatorArgs<DummyExtraHeader, DummyExtraHeader, sizeof(DummyEntry)>;

class DummyAllocatorBlock : public TreeNodesAllocator<DummyAllocatorBlockArgs> {
 public:
  using base = TreeNodesAllocator<DummyAllocatorBlockArgs>;

  DummyAllocatorBlock(std::shared_ptr<Block> block) : base(std::move(block)) {}

  const HeapHeader* get_heap_header() const { return base::heap_header(); }
  const HeapFreelistEntry* get_freelist_entry(uint32_t index) const { return base::get_freelist_entry(index); }
};

class TreeNodesAllocatorFixture : public MetadataBlockFixture {
 public:
  TreeNodesAllocatorFixture() { allocator.Init(); }

  std::shared_ptr<TestBlock> block = LoadMetadataBlock(0);
  DummyAllocatorBlock allocator{block};

  static constexpr size_t kTotalBytes = (size_t{1} << log2_size(BlockSize::Logical)) - sizeof(MetadataBlockHeader) -
                                        sizeof(DummyExtraHeader) - sizeof(DummyTreeHeader) - sizeof(HeapHeader);
  static constexpr size_t kMaxEntriesCount = kTotalBytes / 0x10;
  static constexpr size_t kOffset = sizeof(MetadataBlockHeader) + sizeof(DummyExtraHeader);
};

}  // namespace

TEST_CASE_METHOD(TreeNodesAllocatorFixture,
                 "TreeNodesAllocator initializes heap metadata",
                 "[tree-nodes-allocator][white-box]") {
  auto* heap_header = allocator.get_heap_header();
  REQUIRE(heap_header->freelist_head.value() == 0);
  REQUIRE(heap_header->allocated_entries.value() == 0);
  REQUIRE(heap_header->total_bytes.value() == kTotalBytes);
  REQUIRE(heap_header->start_offset.value() == kOffset);

  auto freelist_entry = allocator.get_freelist_entry(0);
  REQUIRE(freelist_entry->count.value() == kMaxEntriesCount);
  REQUIRE(freelist_entry->next.value() == kMaxEntriesCount);
}

TEST_CASE_METHOD(TreeNodesAllocatorFixture,
                 "TreeNodesAllocator allocates and frees one entry",
                 "[tree-nodes-allocator][unit]") {
  auto* entry = allocator.Alloc<DummyEntry>(1);
  REQUIRE(entry == block->get_object<DummyEntry>(kOffset));
  REQUIRE(allocator.to_offset(entry) == kOffset);

  auto* heap_header = allocator.get_heap_header();
  REQUIRE(heap_header->freelist_head.value() == 1);
  REQUIRE(heap_header->allocated_entries.value() == 1);
  REQUIRE(heap_header->total_bytes.value() == kTotalBytes);
  REQUIRE(heap_header->start_offset.value() == kOffset);

  auto freelist_entry1 = allocator.get_freelist_entry(1);
  REQUIRE(freelist_entry1->count.value() == kMaxEntriesCount - 1);
  REQUIRE(freelist_entry1->next.value() == kMaxEntriesCount);

  allocator.Free(entry, 1);
  REQUIRE(heap_header->freelist_head.value() == 0);
  REQUIRE(heap_header->allocated_entries.value() == 0);

  auto freelist_entry0 = allocator.get_freelist_entry(0);
  REQUIRE(freelist_entry0->count.value() == kMaxEntriesCount);
  REQUIRE(freelist_entry0->next.value() == kMaxEntriesCount);
}

TEST_CASE_METHOD(TreeNodesAllocatorFixture,
                 "TreeNodesAllocator coalesces multiple freed entries",
                 "[tree-nodes-allocator][unit]") {
  auto* entry1 = allocator.Alloc<DummyEntry>(1);
  REQUIRE(allocator.to_offset(entry1) == kOffset);
  auto* entry2 = allocator.Alloc<DummyEntry>(2);
  REQUIRE(entry2 == entry1 + 1);
  auto* entry3 = allocator.Alloc<DummyEntry>(1);
  REQUIRE(entry3 == entry2 + 2);

  auto* heap_header = allocator.get_heap_header();
  REQUIRE(heap_header->freelist_head.value() == 4);
  REQUIRE(heap_header->allocated_entries.value() == 4);
  REQUIRE(heap_header->total_bytes.value() == kTotalBytes);
  REQUIRE(heap_header->start_offset.value() == kOffset);
  auto freelist_entry4 = allocator.get_freelist_entry(4);
  REQUIRE(freelist_entry4->count.value() == kMaxEntriesCount - 4);
  REQUIRE(freelist_entry4->next.value() == kMaxEntriesCount);

  allocator.Free(entry2, 2);
  REQUIRE(heap_header->freelist_head.value() == 1);
  REQUIRE(heap_header->allocated_entries.value() == 2);
  auto freelist_entry1 = allocator.get_freelist_entry(1);
  REQUIRE(freelist_entry1->count.value() == 2);
  REQUIRE(freelist_entry1->next.value() == 4);
  REQUIRE(freelist_entry4->count.value() == kMaxEntriesCount - 4);
  REQUIRE(freelist_entry4->next.value() == kMaxEntriesCount);

  allocator.Free(entry1, 1);
  REQUIRE(heap_header->freelist_head.value() == 0);
  REQUIRE(heap_header->allocated_entries.value() == 1);
  auto freelist_entry0 = allocator.get_freelist_entry(0);
  REQUIRE(freelist_entry0->count.value() == 3);
  REQUIRE(freelist_entry0->next.value() == 4);
  REQUIRE(freelist_entry4->count.value() == kMaxEntriesCount - 4);
  REQUIRE(freelist_entry4->next.value() == kMaxEntriesCount);

  allocator.Free(entry3, 1);
  REQUIRE(heap_header->freelist_head.value() == 0);
  REQUIRE(heap_header->allocated_entries.value() == 0);
  REQUIRE(freelist_entry0->count.value() == kMaxEntriesCount);
  REQUIRE(freelist_entry0->next.value() == kMaxEntriesCount);
}

TEST_CASE_METHOD(TreeNodesAllocatorFixture, "TreeNodesAllocator fills the heap", "[tree-nodes-allocator][unit]") {
  auto* entry = allocator.Alloc<DummyEntry>(kMaxEntriesCount);
  REQUIRE(allocator.to_offset(entry) == kOffset);
  auto* entry2 = allocator.Alloc<DummyEntry>(1);
  REQUIRE(entry2 == nullptr);
  auto* heap_header = allocator.get_heap_header();
  REQUIRE(heap_header->freelist_head.value() == kMaxEntriesCount);
  REQUIRE(heap_header->allocated_entries.value() == kMaxEntriesCount);

  allocator.Free(entry, kMaxEntriesCount);
  REQUIRE(heap_header->freelist_head.value() == 0);
  REQUIRE(heap_header->allocated_entries.value() == 0);
}

TEST_CASE_METHOD(TreeNodesAllocatorFixture,
                 "TreeNodesAllocator rejects allocations larger than the heap",
                 "[tree-nodes-allocator][unit]") {
  auto* entry = allocator.Alloc<DummyEntry>(kMaxEntriesCount + 1);
  REQUIRE(entry == nullptr);
}
