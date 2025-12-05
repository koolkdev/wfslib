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

#include "free_blocks_tree.h"

#include "utils/test_area.h"
#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"

TEST_CASE("FreeBlocksAllocatorTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto area_block = TestBlock::LoadMetadataBlock(test_device, 0);
  auto allocator_block = TestBlock::LoadMetadataBlock(test_device, 1);
  auto area = std::make_shared<TestArea>(std::move(area_block));
  TestFreeBlocksAllocator allocator{allocator_block, test_device, area};

  SECTION("Alloc single blocks from cache") {
    const uint32_t kCacheBlocksCount = 100;
    REQUIRE(allocator.Init(kCacheBlocksCount));
    // Set cache available
    allocator.set_blocks_cache_size_log2(1);

    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);

    // Empty tree, should fail alloc from it
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, false));

    auto blocks = allocator.AllocBlocks(kCacheBlocksCount, BlockType::Single, true);
    REQUIRE(blocks);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 0);

    REQUIRE(blocks->size() == kCacheBlocksCount);
    REQUIRE(std::ranges::equal(*blocks, std::views::iota(allocator.initial_frees_block_number(),
                                                         allocator.initial_frees_block_number() + kCacheBlocksCount)));

    // no more free blocks
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
  }

  SECTION("Alloc single blocks from cache one by one") {
    const uint32_t kCacheBlocksCount = 100;
    REQUIRE(allocator.Init(kCacheBlocksCount));
    // Set cache available
    allocator.set_blocks_cache_size_log2(1);

    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);

    // Empty tree, should fail alloc from it
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, false));

    for (uint32_t i = 0; i < kCacheBlocksCount; ++i) {
      auto blocks = allocator.AllocBlocks(1, BlockType::Single, true);
      REQUIRE(blocks);
      REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount - i - 1);
      REQUIRE(blocks->size() == 1);
      REQUIRE((*blocks)[0] == allocator.initial_frees_block_number() + i);
    }

    // no more free blocks
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
  }

  SECTION("Alloc large block from cache") {
    const uint32_t kCacheBlocksCount = 100;
    REQUIRE(allocator.Init(kCacheBlocksCount));
    // Set cache available
    allocator.set_blocks_cache_size_log2(1);

    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);
    // Can't alloc non single block from cache
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Large, true));
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kCacheBlocksCount);
  }

  SECTION("Alloc single blocks from tree") {
    const uint32_t kTreeBlocksCount = 100;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // no cache
    allocator.set_blocks_cache_size_log2(0);

    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

    auto blocks = allocator.AllocBlocks(kTreeBlocksCount, BlockType::Single, true);
    REQUIRE(blocks);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 0);

    REQUIRE(blocks->size() == kTreeBlocksCount);
    REQUIRE(std::ranges::equal(*blocks, std::views::iota(allocator.initial_frees_block_number(),
                                                         allocator.initial_frees_block_number() + kTreeBlocksCount)));

    // no more free blocks
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
  }

  SECTION("Alloc single blocks from tree one by one") {
    const uint32_t kTreeBlocksCount = 100;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // no cache
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
    // Since we allocate small block seperatly we will consume the smallest bucket first and won't alloc it in serial
    // order.
    std::ranges::sort(allocated_blocks);
    REQUIRE(std::ranges::equal(allocated_blocks,
                               std::views::iota(allocator.initial_frees_block_number(),
                                                allocator.initial_frees_block_number() + kTreeBlocksCount)));

    // no more free blocks
    REQUIRE_FALSE(allocator.AllocBlocks(1, BlockType::Single, true));
  }

  SECTION("Initialize huge area") {
    const uint32_t kTreeBlocksCount = (1 << 28) - (10);  // max size minus few blocks
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
    REQUIRE(extent_indexes.size() == 16);
    REQUIRE(std::ranges::equal(extent_indexes, std::vector<size_t>({0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 5, 4, 3, 2, 1, 0})));
    // minus 2 because the beginning and the end are not aligned.
    uint32_t blocks_to_alloc = (1 << (28 - 6)) - 2;
    auto blocks = allocator.AllocBlocks(blocks_to_alloc, BlockType::Cluster, false);
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == blocks_to_alloc);
    // should be first aligned block number
    REQUIRE((*blocks)[0] == 1 << 6);
    REQUIRE(std::ranges::equal(*blocks, std::views::transform(std::views::iota(uint32_t{0}, blocks_to_alloc),
                                                              [](auto i) { return (i + 1) << 6; })));
    REQUIRE((kTreeBlocksCount - (blocks_to_alloc << 6)) == allocator.GetHeader()->free_blocks_count.value());

    extent_indexes.clear();
    for (const auto& extent : tree) {
      extent_indexes.push_back(extent.bucket_index);
    }

    // Should have allocated all the larger allocation sizes
    auto single_block = allocator.AllocBlocks(1, BlockType::Single, false);
    REQUIRE(std::ranges::equal(extent_indexes, std::vector<size_t>({0, 1, 1, 0})));
    REQUIRE(single_block.has_value());
    // should allocate first block now
    REQUIRE(single_block->at(0) == allocator.initial_frees_block_number());
  }

  SECTION("Free single blocks at sequantial order") {
    const uint32_t kBlocksToFree = 10000;
    // For tree structures.
    const uint32_t kCacheBlocksCount = 600;
    REQUIRE(allocator.Init(kCacheBlocksCount));

    REQUIRE(std::ranges::all_of(std::views::iota(uint32_t{0}, kBlocksToFree), [&allocator](uint32_t i) -> bool {
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

    REQUIRE(extent_indexes.size() == 7);
    REQUIRE(std::ranges::equal(extent_indexes, std::vector<size_t>({0, 1, 2, 3, 2, 1, 0})));
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() - allocator.GetHeader()->free_blocks_cache_count.value() ==
            kBlocksToFree);
  }

  SECTION("Free single blocks at random order") {
    const uint32_t kBlocksToFree = 10000;
    // For tree structures.
    const uint32_t kCacheBlocksCount = 600;
    REQUIRE(allocator.Init(kCacheBlocksCount));

    auto blocks_to_free = std::ranges::to<std::vector>(std::views::iota(uint32_t{0}, kBlocksToFree));

    std::ranges::shuffle(blocks_to_free, std::default_random_engine{Catch::getSeed()});
    REQUIRE(std::ranges::all_of(blocks_to_free, [&allocator](uint32_t i) -> bool {
      return allocator.AddFreeBlocks({i + allocator.initial_frees_block_number() + kCacheBlocksCount, 1});
    }));

    auto current_block_number = allocator.initial_frees_block_number() + kCacheBlocksCount;
    FreeBlocksTree tree{&allocator};
    std::vector<size_t> extent_indexes;
    uint32_t released_ftrees = 0;
    for (const auto& extent : tree) {
      if (extent.block_number() < allocator.initial_frees_block_number() + kCacheBlocksCount) {
        // The released FTrees that were alloced from the cache are released back to the tree.
        released_ftrees += extent.blocks_count();
        continue;
      }
      REQUIRE(extent.block_number() == current_block_number);
      current_block_number += extent.blocks_count();
      extent_indexes.push_back(extent.bucket_index);
    }
    REQUIRE(current_block_number == allocator.initial_frees_block_number() + kCacheBlocksCount + kBlocksToFree);

    REQUIRE(extent_indexes.size() == 7);
    REQUIRE(std::ranges::equal(extent_indexes, std::vector<size_t>({0, 1, 2, 3, 2, 1, 0})));
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() - allocator.GetHeader()->free_blocks_cache_count.value() -
                released_ftrees ==
            kBlocksToFree);
  }

  SECTION("Replansish cache") {
    const uint32_t kTreeBlocksCount = 10000;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // cache size (8)
    allocator.set_blocks_cache_size_log2(3);

    auto blocks = allocator.AllocBlocks(1, BlockType::Single, true);
    REQUIRE(blocks);
    // Should be aligned to data from big enough extent
    REQUIRE(blocks->at(0) == 8);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount - 1);
    REQUIRE(allocator.GetHeader()->free_blocks_cache.value() == 9);
    REQUIRE(allocator.GetHeader()->free_blocks_cache_count.value() == 7);
    REQUIRE_FALSE(allocator.IsRangeIsFree({9, 1}));
    REQUIRE(allocator.IsRangeIsFree({3, 1}));
  }

  SECTION("Alloc area blocks") {
    const uint32_t kTreeBlocksCount = 100;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // no cache
    allocator.set_blocks_cache_size_log2(0);

    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

    auto ranges = allocator.AllocAreaBlocks(kTreeBlocksCount, BlockType::Single);
    REQUIRE(ranges);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == 0);

    REQUIRE(ranges->size() == 1);
    REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number());
    REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount);
  }

  SECTION("Alloc half area blocks") {
    const uint32_t kTreeBlocksCount = 100;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // no cache
    allocator.set_blocks_cache_size_log2(0);

    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount);

    auto ranges = allocator.AllocAreaBlocks(kTreeBlocksCount / 2, BlockType::Single);
    REQUIRE(ranges);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount / 2);

    // Should be aligned to end
    REQUIRE(ranges->size() == 1);
    REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number() + kTreeBlocksCount / 2);
    REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount / 2);
    REQUIRE(allocator.IsRangeIsFree({allocator.initial_frees_block_number(), kTreeBlocksCount / 2}));
    REQUIRE_FALSE(
        allocator.IsRangeIsFree({allocator.initial_frees_block_number() + kTreeBlocksCount / 2, kTreeBlocksCount / 2}));
  }

  SECTION("Alloc fragmented area") {
    const uint32_t kTreeBlocksCount = 100;
    const uint32_t kAreaBlocksToAlloc = 150;
    const uint32_t kSecondFragmentAddress = 1000;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // no cache
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

  SECTION("Alloc fragmented area 2") {
    const uint32_t kTreeBlocksCount = 100;
    const uint32_t kAreaBlocksToAlloc = 150;
    const uint32_t kSecondFragmentAddress = 1000;
    const uint32_t kThirdFragmentAddress = 2000;
    REQUIRE(allocator.Init(0, kTreeBlocksCount));
    // no cache
    allocator.set_blocks_cache_size_log2(0);

    REQUIRE(allocator.AddFreeBlocks({kSecondFragmentAddress, kTreeBlocksCount / 2}));
    REQUIRE(allocator.AddFreeBlocks({kThirdFragmentAddress, kTreeBlocksCount}));
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount * 2.5);

    auto ranges = allocator.AllocAreaBlocks(kAreaBlocksToAlloc, BlockType::Single);
    REQUIRE(ranges);
    REQUIRE(allocator.GetHeader()->free_blocks_count.value() == kTreeBlocksCount * 2.5 - kAreaBlocksToAlloc);

    // Second fragment should be ignore because it is the smallest
    REQUIRE(ranges->size() == 2);
    REQUIRE(ranges->at(0).block_number == allocator.initial_frees_block_number() + kTreeBlocksCount / 2);
    REQUIRE(ranges->at(0).blocks_count == kTreeBlocksCount / 2);
    REQUIRE(ranges->at(1).block_number == kThirdFragmentAddress);
    REQUIRE(ranges->at(1).blocks_count == kTreeBlocksCount);
  }
}
