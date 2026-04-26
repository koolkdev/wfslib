/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <ranges>

#include "eptree.h"
#include "free_blocks_tree.h"
#include "free_blocks_tree_bucket.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates all single blocks from cache",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kCacheBlocksCount = 100;
  REQUIRE(allocator.Init(kCacheBlocksCount));
  allocator.set_blocks_cache_size_log2(1);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, false));

  auto blocks = allocator.AllocBlocks(kCacheBlocksCount, BlockType::Single, true);
  REQUIRE(blocks);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 0);
  REQUIRE(*blocks == SequentialKeys(kCacheBlocksCount, allocator.initial_frees_block_number()));
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates cache blocks one by one",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kCacheBlocksCount = 100;
  REQUIRE(allocator.Init(kCacheBlocksCount));
  allocator.set_blocks_cache_size_log2(1);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, false));

  for (uint32_t i = 0; i < kCacheBlocksCount; ++i) {
    auto blocks = allocator.AllocBlocks(1, BlockType::Single, true);
    REQUIRE(blocks);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount - i - 1);
    REQUIRE(*blocks == std::vector<uint32_t>{allocator.initial_frees_block_number() + i});
  }
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator does not allocate large blocks from cache",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kCacheBlocksCount = 100;
  REQUIRE(allocator.Init(kCacheBlocksCount));
  allocator.set_blocks_cache_size_log2(1);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Large, true));
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates all single blocks from tree",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 100;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

  auto blocks = allocator.AllocBlocks(kTreeBlocksCount, BlockType::Single, true);
  REQUIRE(blocks);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 0);
  REQUIRE(*blocks == SequentialKeys(kTreeBlocksCount, allocator.initial_frees_block_number()));
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates tree blocks one by one",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 100;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

  std::vector<uint32_t> allocated_blocks;
  for (uint32_t i = 0; i < kTreeBlocksCount; ++i) {
    auto blocks = allocator.AllocBlocks(1, BlockType::Single, true);
    REQUIRE(blocks);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount - i - 1);
    REQUIRE(blocks->size() == 1);
    allocated_blocks.push_back((*blocks)[0]);
  }
  std::ranges::sort(allocated_blocks);
  REQUIRE(allocated_blocks == SequentialKeys(kTreeBlocksCount, allocator.initial_frees_block_number()));
  REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator initializes the largest representable area",
                 "[free-blocks][allocator][stress]") {
  const uint32_t kTreeBlocksCount = (1 << 28) - 10;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));

  auto current_block_number = allocator.initial_frees_block_number();
  FreeBlocksTree tree{&allocator};
  std::vector<size_t> extent_indexes;
  for (const auto& extent : tree) {
    REQUIRE(extent.block_number() == current_block_number);
    current_block_number += extent.blocks_count();
    extent_indexes.push_back(extent.bucket_index);
  }

  REQUIRE(current_block_number == allocator.initial_frees_block_number() + kTreeBlocksCount);
  REQUIRE(extent_indexes == std::vector<size_t>({0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 5, 4, 3, 2, 1, 0}));

  uint32_t blocks_to_alloc = (1 << (28 - 6)) - 2;
  auto blocks = allocator.AllocBlocks(blocks_to_alloc, BlockType::Cluster, false);
  REQUIRE(blocks);
  REQUIRE(blocks->size() == blocks_to_alloc);
  REQUIRE(blocks->at(0) == 1 << 6);
  REQUIRE(*blocks == CollectRange(std::views::iota(uint32_t{0}, blocks_to_alloc), [](auto i) { return (i + 1) << 6; }));
  REQUIRE((kTreeBlocksCount - (blocks_to_alloc << 6)) == allocator.GetHeader()->free_blocks_count.value());

  extent_indexes.clear();
  for (const auto& extent : tree) {
    extent_indexes.push_back(extent.bucket_index);
  }

  auto single_block = allocator.AllocBlocks(1, BlockType::Single, false);
  REQUIRE(extent_indexes == std::vector<size_t>({0, 1, 1, 0}));
  REQUIRE(single_block);
  REQUIRE(single_block->at(0) == allocator.initial_frees_block_number());
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator merges sequentially freed single blocks",
                 "[free-blocks][allocator][integration]") {
  const uint32_t kBlocksToFree = 10000;
  const uint32_t kCacheBlocksCount = 600;
  REQUIRE(allocator.Init(kCacheBlocksCount));

  REQUIRE(std::ranges::all_of(std::views::iota(uint32_t{0}, kBlocksToFree), [this](uint32_t i) {
    return allocator.AddFreeBlocks({i + allocator.initial_frees_block_number() + kCacheBlocksCount, 1});
  }));

  auto current_block_number = allocator.initial_frees_block_number() + kCacheBlocksCount;
  FreeBlocksTree tree{&allocator};
  std::vector<size_t> extent_indexes;
  for (const auto& extent : tree) {
    REQUIRE(extent.block_number() == current_block_number);
    current_block_number += extent.blocks_count();
    extent_indexes.push_back(extent.bucket_index);
  }
  REQUIRE(current_block_number == allocator.initial_frees_block_number() + kCacheBlocksCount + kBlocksToFree);
  REQUIRE(extent_indexes == std::vector<size_t>({0, 1, 2, 3, 2, 1, 0}));
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() - allocator.GetHeader()->free_blocks_cache_count.value() ==
          kBlocksToFree);
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator merges randomly freed single blocks",
                 "[free-blocks][allocator][integration]") {
  const uint32_t kBlocksToFree = 10000;
  const uint32_t kCacheBlocksCount = 600;
  REQUIRE(allocator.Init(kCacheBlocksCount));

  auto blocks_to_free = SequentialKeys(kBlocksToFree);
  std::ranges::shuffle(blocks_to_free, std::default_random_engine{Catch::getSeed()});
  REQUIRE(std::ranges::all_of(blocks_to_free, [this](uint32_t i) {
    return allocator.AddFreeBlocks({i + allocator.initial_frees_block_number() + kCacheBlocksCount, 1});
  }));

  auto current_block_number = allocator.initial_frees_block_number() + kCacheBlocksCount;
  FreeBlocksTree tree{&allocator};
  std::vector<size_t> extent_indexes;
  uint32_t released_ftrees = 0;
  for (const auto& extent : tree) {
    if (extent.block_number() < allocator.initial_frees_block_number() + kCacheBlocksCount) {
      released_ftrees += extent.blocks_count();
      continue;
    }
    REQUIRE(extent.block_number() == current_block_number);
    current_block_number += extent.blocks_count();
    extent_indexes.push_back(extent.bucket_index);
  }
  REQUIRE(current_block_number == allocator.initial_frees_block_number() + kCacheBlocksCount + kBlocksToFree);
  REQUIRE(extent_indexes == std::vector<size_t>({0, 1, 2, 3, 2, 1, 0}));
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() - allocator.GetHeader()->free_blocks_cache_count.value() -
              released_ftrees ==
          kBlocksToFree);
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator replenishes cache from the tree",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 10000;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(3);

  auto blocks = allocator.AllocBlocks(1, BlockType::Single, true);
  REQUIRE(blocks);
  REQUIRE(blocks->at(0) == 8);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount - 1);
  REQUIRE(allocator.GetHeader()->free_blocks_cache.value() == 9);
  REQUIRE(allocator.GetHeader()->free_blocks_cache_count.value() == 7);
  REQUIRE_FALSE(allocator.IsRangeIsFree({9, 1}));
  REQUIRE(allocator.IsRangeIsFree({3, 1}));
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates one area range",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 100;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

  auto ranges = allocator.AllocAreaBlocks(kTreeBlocksCount, BlockType::Single);
  REQUIRE(ranges);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 0);
  REQUIRE(ranges->size() == 1);
  REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number());
  REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount);
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates the second half of one area range",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 100;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

  auto ranges = allocator.AllocAreaBlocks(kTreeBlocksCount / 2, BlockType::Single);
  REQUIRE(ranges);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount / 2);
  REQUIRE(ranges->size() == 1);
  REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number() + kTreeBlocksCount / 2);
  REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount / 2);
  REQUIRE(allocator.IsRangeIsFree({allocator.initial_frees_block_number(), kTreeBlocksCount / 2}));
  REQUIRE_FALSE(
      allocator.IsRangeIsFree({allocator.initial_frees_block_number() + kTreeBlocksCount / 2, kTreeBlocksCount / 2}));
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator allocates fragmented area ranges",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 100;
  const uint32_t kAreaBlocksToAlloc = 150;
  const uint32_t kSecondFragmentAddress = 1000;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.AddFreeBlocks({kSecondFragmentAddress, kTreeBlocksCount}));
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount * 2);

  auto ranges = allocator.AllocAreaBlocks(kAreaBlocksToAlloc, BlockType::Single);
  REQUIRE(ranges);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount * 2 - kAreaBlocksToAlloc);
  REQUIRE(ranges->size() == 2);
  REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number() + kTreeBlocksCount / 2);
  REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount / 2);
  REQUIRE(ranges->at(1).block_number == kSecondFragmentAddress);
  REQUIRE(ranges->at(1).blocks_count == kTreeBlocksCount);
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator skips too-small area fragments",
                 "[free-blocks][allocator][unit]") {
  const uint32_t kTreeBlocksCount = 100;
  const uint32_t kAreaBlocksToAlloc = 150;
  const uint32_t kSecondFragmentAddress = 1000;
  const uint32_t kThirdFragmentAddress = 2000;
  REQUIRE(allocator.Init(0, kTreeBlocksCount));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.AddFreeBlocks({kSecondFragmentAddress, kTreeBlocksCount / 2}));
  REQUIRE(allocator.AddFreeBlocks({kThirdFragmentAddress, kTreeBlocksCount}));
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount * 5 / 2);

  auto ranges = allocator.AllocAreaBlocks(kAreaBlocksToAlloc, BlockType::Single);
  REQUIRE(ranges);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount * 5 / 2 - kAreaBlocksToAlloc);
  REQUIRE(ranges->size() == 2);
  REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number() + kTreeBlocksCount / 2);
  REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount / 2);
  REQUIRE(ranges->at(1).block_number == kThirdFragmentAddress);
  REQUIRE(ranges->at(1).blocks_count == kTreeBlocksCount);
}

TEST_CASE_METHOD(FreeBlocksAllocatorFixture,
                 "FreeBlocksAllocator finds a nearby free extent without an exact key match",
                 "[free-blocks][allocator][regression]") {
  REQUIRE(allocator.Init(0));

  FreeBlocksTreeBucket bucket{&allocator, 0};
  REQUIRE(bucket.insert({10, nibble{0}}));

  std::vector<FreeBlocksExtentInfo> allocated;
  REQUIRE(allocator.FindSmallestFreeBlockExtent(11, allocated) == 10);
  REQUIRE(allocated == std::vector<FreeBlocksExtentInfo>{{10, 1, 0}});
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator recreates a collapsed EPTree",
                 "[free-blocks][allocator][regression]") {
  REQUIRE(allocator.Init(1000000));

  EPTree eptree{&allocator};
  constexpr uint32_t kExtraEntries = 1000;
  for (uint32_t i = 1; i <= kExtraEntries; ++i) {
    REQUIRE(eptree.insert({i, i + 1000}));
  }

  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  for (uint32_t i = 1; i <= kExtraEntries; ++i) {
    REQUIRE(eptree.erase(i, blocks_to_delete));
  }
  REQUIRE((eptree.tree_header()->depth.value() > 1 || eptree.tree_header()->current_tree.tree_depth.value() > 0));

  allocator.RecreateEPTreeForTesting();

  EPTree recreated_eptree{&allocator};
  REQUIRE(recreated_eptree.tree_header()->depth.value() == 1);
  REQUIRE(recreated_eptree.tree_header()->current_tree.tree_depth.value() == 0);
  REQUIRE(CollectKeyValues(recreated_eptree) ==
          std::vector<std::pair<uint32_t, uint32_t>>{{0, allocator.initial_ftrees_block_number()}});
}

TEST_CASE_METHOD(AreaAllocatorFixture,
                 "FreeBlocksAllocator trims fragmented area allocation from the lowest selected range",
                 "[free-blocks][allocator][regression]") {
  REQUIRE(allocator.Init(0));
  allocator.set_blocks_cache_size_log2(0);

  REQUIRE(allocator.AddFreeBlocks({1000, 100}));
  REQUIRE(allocator.AddFreeBlocks({2000, 100}));

  auto ranges = allocator.AllocAreaBlocks(150, BlockType::Single);
  REQUIRE(ranges);
  REQUIRE(ranges->size() == 2);
  REQUIRE(ranges->at(0).block_number == 1050);
  REQUIRE(ranges->at(0).blocks_count == 50);
  REQUIRE(ranges->at(1).block_number == 2000);
  REQUIRE(ranges->at(1).blocks_count == 100);
  REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 50);
  REQUIRE(allocator.IsRangeIsFree({1000, 50}));
  REQUIRE_FALSE(allocator.IsRangeIsFree({1050, 50}));
  REQUIRE_FALSE(allocator.IsRangeIsFree({2000, 100}));
}
