/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <ranges>

#include "../src/free_blocks_tree_bucket.h"

#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"
#include "test_metadata_block.h"
#include "test_utils.h"

TEST_CASE("FreeBlocksTreeBucketTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto allocator_block = TestMetadataBlock::LoadBlock(test_device, 0);
  TestFreeBlocksAllocator allocator{allocator_block, test_device};
  allocator.Init(1000000);
  EPTree eptree{&allocator};
  eptree.Init();
  auto initial_ftrees_block_number = allocator.AllocFreeBlockFromCache();
  auto initial_ftrees_block = allocator.LoadAllocatorBlock(initial_ftrees_block_number, true);
  FTreesBlock{initial_ftrees_block}.Init();
  REQUIRE(eptree.insert({0, initial_ftrees_block_number}));

  FreeBlocksTreeBucket bucket{&allocator, 3};

  SECTION("insert items sorted") {
    constexpr int kItemsCount = 800;  // 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
    }

    REQUIRE(std::ranges::equal(
        std::views::transform(bucket,
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i, static_cast<nibble>(i % 16), static_cast<size_t>(3)};
        })));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(bucket.find(i, true)->key == i);
    }
  }

  SECTION("insert items unsorted") {
    constexpr int kItemsCount = 600 * 300;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(bucket.insert({key, nibble{key % 16}}));
    }

    // Check that the tree is sorted
    auto keys = std::views::transform(
        bucket, [](const FTrees::iterator::value_type& extent) -> uint32_t { return extent.key; });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    REQUIRE(std::ranges::equal(sorted_keys, keys));

    // Check the values
    REQUIRE(std::ranges::equal(
        std::views::transform(bucket,
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i, static_cast<nibble>(i % 16), static_cast<size_t>(3)};
        })));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(bucket.find(i, true)->key == i);
    }
  }

  SECTION("erase items randomly") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
    }
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    auto middle = unsorted_keys.begin() + kItemsCount / 2;
    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    // Remove half the items first
    for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
      REQUIRE(bucket.erase(key, blocks_to_delete));
    }

    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_keys.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    // Ensure that the right items were deleted
    REQUIRE(std::ranges::equal(
        std::views::transform(bucket, [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        sorted_upper_half));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(bucket.erase(key, blocks_to_delete));
    }

    // Should be empty
    REQUIRE(bucket.begin() == bucket.end());

    auto blocks_numbers_to_delete =
        std::views::transform(blocks_to_delete, [](const FreeBlocksRangeInfo& range) { return range.block_number; }) |
        std::ranges::to<std::vector>();

    std::ranges::sort(blocks_numbers_to_delete);
    // Check deleted blocks, everything beside first ftree should be deleted
    REQUIRE(std::ranges::equal(blocks_numbers_to_delete,
                               std::views::iota(initial_ftrees_block_number + 1, allocator.AllocFreeBlockFromCache())));
  }

  SECTION("empty ftree find") {
    // Have three empty ftrees, check that items are added to the correct ones, and check how find works with them.
    auto ftree_100_to_200_block_number = allocator.AllocFreeBlockFromCache();
    auto ftree_100_to_200_block = allocator.LoadAllocatorBlock(ftree_100_to_200_block_number, true);
    FTreesBlock{ftree_100_to_200_block}.Init();
    REQUIRE(eptree.insert({100, ftree_100_to_200_block_number}));
    auto ftree_200_plus_block_number = allocator.AllocFreeBlockFromCache();
    auto ftree_200_plus_block = allocator.LoadAllocatorBlock(ftree_200_plus_block_number, true);
    FTreesBlock{ftree_200_plus_block}.Init();
    REQUIRE(eptree.insert({200, ftree_200_plus_block_number}));

    FTree ftree1{initial_ftrees_block, 3}, ftree2{ftree_100_to_200_block, 3}, ftree3{ftree_200_plus_block, 3};
    REQUIRE(ftree1.empty());
    REQUIRE(ftree2.empty());
    REQUIRE(ftree3.empty());
    REQUIRE(bucket.find(150, false) == bucket.end());

    // insert to first ftree
    REQUIRE(bucket.insert({50, nibble{0}}));
    REQUIRE(ftree1.size() == 1);
    REQUIRE(ftree2.empty());
    REQUIRE(ftree3.empty());
    REQUIRE(bucket.find(150, false) != bucket.end());
    REQUIRE(bucket.find(150, false)->key == 50);
    REQUIRE(bucket.find(25, false)->key == 50);
    REQUIRE(std::ranges::equal(
        std::views::transform(bucket, [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{50}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(bucket),
                              [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{50}));

    REQUIRE(bucket.insert({160, nibble{0}}));
    REQUIRE(ftree1.size() == 1);
    REQUIRE(ftree2.size() == 1);
    REQUIRE(ftree3.empty());
    REQUIRE(bucket.find(150, false)->key == 50);
    REQUIRE(bucket.find(250, false)->key == 160);
    REQUIRE(std::ranges::equal(
        std::views::transform(bucket, [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{50, 160}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(bucket),
                              [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{160, 50}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(bucket.rbegin(), bucket.rend()),
                              [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{160, 50}));

    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    REQUIRE(bucket.erase(50, blocks_to_delete));
    REQUIRE(blocks_to_delete.empty());  // first block should delete it
    REQUIRE(ftree1.empty());
    REQUIRE(ftree2.size() == 1);
    REQUIRE(ftree3.empty());
    REQUIRE(bucket.find(150, false)->key == 160);
    REQUIRE(bucket.find(250, false)->key == 160);
    REQUIRE(bucket.find(25, false)->key == 160);
    REQUIRE(std::ranges::equal(
        std::views::transform(bucket, [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{160}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(bucket),
                              [](const FTrees::iterator::value_type& extent) -> int { return extent.key; }),
        std::list<uint32_t>{160}));
  }
}
