/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include "../src/sub_block_allocator.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"

namespace {

struct DummyExtraHeader {
  uint32_be_t a, b;
};

class TestSubBlockAllocator : public SubBlockAllocator<DummyExtraHeader> {
 public:
  using base = SubBlockAllocator<DummyExtraHeader>;

  TestSubBlockAllocator(std::shared_ptr<Block> block) : base(std::move(block)) {}

  const auto* get_allocator_header() const { return base::header(); }
  const auto* get_freelist_entry(uint32_t offset) const {
    return block()->get_object<SubBlockAllocatorFreeListEntry>(offset);
  }
};

}  // namespace

TEST_CASE("SubBlockAllocatorTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto block = TestBlock::LoadMetadataBlock(test_device, 0);
  TestSubBlockAllocator allocator{block};
  allocator.Init();
  REQUIRE(sizeof(MetadataBlockHeader) + sizeof(SubBlockAllocatorStruct) + sizeof(DummyExtraHeader) == 64);

  SECTION("Check initial heap state") {
    auto* allocator_header = allocator.get_allocator_header();
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[1].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[2].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[3].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[4].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[5].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[6].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[7].free_blocks_count.value() == 7);

    CHECK(allocator_header->free_list[3].head.value() == 0x40);
    CHECK(allocator_header->free_list[4].head.value() == 0x80);
    CHECK(allocator_header->free_list[5].head.value() == 0x100);
    CHECK(allocator_header->free_list[6].head.value() == 0x200);
    CHECK(allocator_header->free_list[7].head.value() == 0x400);

    auto one_freelist_entry = allocator.get_freelist_entry(allocator_header->free_list[3].head.value());
    CHECK(one_freelist_entry->next.value() == allocator_header->free_list[3].head.value());
    CHECK(one_freelist_entry->prev.value() == allocator_header->free_list[3].head.value());

    auto current_freelist_entry = allocator.get_freelist_entry(allocator_header->free_list[7].head.value());
    CHECK(current_freelist_entry->next.value() == 0x800);
    CHECK(current_freelist_entry->prev.value() == 0x1c00);
    current_freelist_entry = allocator.get_freelist_entry(current_freelist_entry->next.value());
    CHECK(current_freelist_entry->next.value() == 0xc00);
    CHECK(current_freelist_entry->prev.value() == 0x400);
    current_freelist_entry = allocator.get_freelist_entry(current_freelist_entry->next.value());
    CHECK(current_freelist_entry->next.value() == 0x1000);
    CHECK(current_freelist_entry->prev.value() == 0x800);
    current_freelist_entry = allocator.get_freelist_entry(current_freelist_entry->next.value());
    CHECK(current_freelist_entry->next.value() == 0x1400);
    CHECK(current_freelist_entry->prev.value() == 0xc00);
    current_freelist_entry = allocator.get_freelist_entry(current_freelist_entry->next.value());
    CHECK(current_freelist_entry->next.value() == 0x1800);
    CHECK(current_freelist_entry->prev.value() == 0x1000);
    current_freelist_entry = allocator.get_freelist_entry(current_freelist_entry->next.value());
    CHECK(current_freelist_entry->next.value() == 0x1c00);
    CHECK(current_freelist_entry->prev.value() == 0x1400);
    current_freelist_entry = allocator.get_freelist_entry(current_freelist_entry->next.value());
    CHECK(current_freelist_entry->next.value() == 0x400);
    CHECK(current_freelist_entry->prev.value() == 0x1800);
  }

  SECTION("Alloc and free one of each size") {
    auto* allocator_header = allocator.get_allocator_header();
    for (int size = 6; size <= 10; ++size) {
      auto offset = allocator.Alloc(uint16_t{1} << size);
      REQUIRE(offset.has_value());
      CHECK(*offset == uint16_t{1} << size);
      if (size == 10)
        CHECK(allocator_header->free_list[size - 3].free_blocks_count.value() == 6);
      else
        CHECK(allocator_header->free_list[size - 3].free_blocks_count.value() == 0);
    }
    for (int size = 6; size <= 10; ++size) {
      allocator.Free(uint16_t{1} << size, uint16_t{1} << size);
      if (size == 10)
        CHECK(allocator_header->free_list[size - 3].free_blocks_count.value() == 7);
      else
        CHECK(allocator_header->free_list[size - 3].free_blocks_count.value() == 1);
    }
  }

  SECTION("Coallese test") {
    auto* allocator_header = allocator.get_allocator_header();
    REQUIRE(*allocator.Alloc(8) == 0x40);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 1);
    REQUIRE(*allocator.Alloc(8) == 0x48);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 0);
    REQUIRE(*allocator.Alloc(8) == 0x50);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 1);
    REQUIRE(*allocator.Alloc(8) == 0x58);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 0);

    allocator.Free(0x48, 8);
    allocator.Free(0x50, 8);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 2);
    allocator.Free(0x40, 8);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[1].free_blocks_count.value() == 1);
    allocator.Free(0x58, 8);
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[3].free_blocks_count.value() == 1);
  }

  SECTION("Fill heap") {
    auto* allocator_header = allocator.get_allocator_header();
    uint16_t current_offset = sizeof(MetadataBlockHeader) + sizeof(SubBlockAllocatorStruct) + sizeof(DummyExtraHeader);
    size_t total_bytes = (1 << log2_size(BlockSize::Logical)) - current_offset;
    for (size_t i = 0; i < (total_bytes >> 3); ++i) {
      auto offset = allocator.Alloc(8);
      REQUIRE(offset.has_value());
      CHECK(*offset == current_offset);
      current_offset += 8;
    }
    // Now should be full
    for (int i = 0; i <= 7; ++i) {
      CHECK(allocator_header->free_list[i].free_blocks_count.value() == 0);
    }

    auto offset = allocator.Alloc(8);
    CHECK(!offset.has_value());

    current_offset = sizeof(MetadataBlockHeader) + sizeof(SubBlockAllocatorStruct) + sizeof(DummyExtraHeader);
    for (size_t i = 0; i < (total_bytes >> 3); ++i) {
      allocator.Free(current_offset, 8);
      current_offset += 8;
    }

    // Should have the intial space
    REQUIRE(allocator_header->free_list[0].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[1].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[2].free_blocks_count.value() == 0);
    REQUIRE(allocator_header->free_list[3].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[4].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[5].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[6].free_blocks_count.value() == 1);
    REQUIRE(allocator_header->free_list[7].free_blocks_count.value() == 7);
  }
}
