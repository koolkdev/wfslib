/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_range_equals.hpp>

#include <algorithm>
#include <random>
#include <ranges>

#include "../src/free_blocks_tree_bucket.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"
#include "utils/test_utils.h"

using Catch::Matchers::RangeEquals;
using Catch::Matchers::UnorderedRangeEquals;

TEST_CASE("FreeBlocksTreeBucketTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto allocator_block = TestBlock::LoadMetadataBlock(test_device, 0);
  TestFreeBlocksAllocator allocator{allocator_block, test_device};
  REQUIRE(allocator.Init(1000000));

  FreeBlocksTreeBucket bucket{&allocator, 3};

  using value_type = FreeBlocksTreeBucket::iterator::value_type;

  SECTION("iterate empty tree") {
    REQUIRE(std::distance(bucket.begin(), bucket.end()) == 0);
  }

  SECTION("insert items sorted") {
    constexpr auto kItemsCount = 600 * 300ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
    }

    CHECK_THAT(bucket, RangeEquals(std::views::iota(0ul, kItemsCount), [](const auto& extent, auto i) {
                 return extent.key == i && extent.value == static_cast<nibble>(i % 16) && extent.bucket_index == 3ul;
               }));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      CHECK(bucket.find(i, true)->key == i);
    }
  }

  SECTION("insert items unsorted") {
    constexpr auto kItemsCount = 600 * 300ul;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(bucket.insert({key, static_cast<nibble>(key % 16)}));
    }

    // Check that the tree is sorted
    auto keys = std::views::transform(bucket, [](const value_type& extent) -> uint32_t { return extent.key; });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    CHECK_THAT(sorted_keys, RangeEquals(keys));

    // Check the values
    CHECK_THAT(bucket, RangeEquals(std::views::iota(0ul, kItemsCount), [](const auto& extent, auto i) {
                 return extent.key == i && extent.value == static_cast<nibble>(i % 16) && extent.bucket_index == 3ul;
               }));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      CHECK(bucket.find(i, true)->key == i);
    }
  }

  SECTION("erase items randomly") {
    constexpr auto kItemsCount = 600 * 300ul;
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
    CHECK_THAT(bucket, RangeEquals(sorted_upper_half, [](const auto& extent, auto key) { return extent.key == key; }));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(bucket.erase(key, blocks_to_delete));
    }

    // Should be empty
    CHECK(bucket.begin() == bucket.end());

    auto blocks_numbers_to_delete =
        std::views::transform(blocks_to_delete, [](const FreeBlocksRangeInfo& range) { return range.block_number; }) |
        std::ranges::to<std::vector>();

    std::ranges::sort(blocks_numbers_to_delete);
    // Check deleted blocks, everything beside first ftree should be deleted
    CHECK_THAT(blocks_numbers_to_delete, UnorderedRangeEquals(std::views::iota(allocator.initial_frees_block_number(),
                                                                               allocator.AllocFreeBlockFromCache())));
  }

  SECTION("empty ftree find") {
    EPTree eptree{&allocator};
    // Have three empty ftrees, check that items are added to the correct ones, and check how find works with them.
    auto ftree_100_to_200_block_number = allocator.AllocFreeBlockFromCache();
    auto ftree_100_to_200_block = allocator.LoadAllocatorBlock(ftree_100_to_200_block_number, true);
    FTreesBlock{ftree_100_to_200_block}.Init();
    REQUIRE(eptree.insert({100, ftree_100_to_200_block_number}));
    auto ftree_200_plus_block_number = allocator.AllocFreeBlockFromCache();
    auto ftree_200_plus_block = allocator.LoadAllocatorBlock(ftree_200_plus_block_number, true);
    FTreesBlock{ftree_200_plus_block}.Init();
    REQUIRE(eptree.insert({200, ftree_200_plus_block_number}));

    auto ftree_0_to_100_block = allocator.LoadAllocatorBlock(allocator.initial_ftrees_block_number(), false);
    FTree ftree1{ftree_0_to_100_block, 3};
    FTree ftree2{ftree_100_to_200_block, 3};
    FTree ftree3{ftree_200_plus_block, 3};

    CHECK(ftree1.empty());
    CHECK(ftree2.empty());
    CHECK(ftree3.empty());
    CHECK(bucket.find(150, false) == bucket.end());

    // insert to first ftree
    REQUIRE(bucket.insert({50, nibble{0}}));
    CHECK(ftree1.size() == 1);
    CHECK(ftree2.empty());
    CHECK(ftree3.empty());
    CHECK(bucket.find(150, false) != bucket.end());
    CHECK(bucket.find(150, false)->key == 50);
    CHECK(bucket.find(25, false)->key == 50);
    CHECK_THAT(bucket,
               RangeEquals(std::list<uint32_t>{50}, [](const auto& extent, auto key) { return extent.key == key; }));
    CHECK_THAT(std::views::reverse(bucket),
               RangeEquals(std::list<uint32_t>{50}, [](const auto& extent, auto key) { return extent.key == key; }));

    REQUIRE(bucket.insert({160, nibble{0}}));
    CHECK(ftree1.size() == 1);
    CHECK(ftree2.size() == 1);
    CHECK(ftree3.empty());
    CHECK(bucket.find(150, false)->key == 50);
    CHECK(bucket.find(250, false)->key == 160);
    CHECK_THAT(bucket, RangeEquals(std::list<uint32_t>{50, 160},
                                   [](const auto& extent, auto key) { return extent.key == key; }));
    CHECK_THAT(std::views::reverse(bucket), RangeEquals(std::list<uint32_t>{160, 50}, [](const auto& extent, auto key) {
                 return extent.key == key;
               }));

    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    REQUIRE(bucket.erase(50, blocks_to_delete));
    CHECK(blocks_to_delete.empty());  // first block should delete it
    CHECK(ftree1.empty());
    CHECK(ftree2.size() == 1);
    CHECK(ftree3.empty());
    CHECK(bucket.find(150, false)->key == 160);
    CHECK(bucket.find(250, false)->key == 160);
    CHECK(bucket.find(25, false)->key == 160);
    CHECK_THAT(bucket,
               RangeEquals(std::list<uint32_t>{160}, [](const auto& extent, auto key) { return extent.key == key; }));
    CHECK_THAT(std::views::reverse(bucket),
               RangeEquals(std::list<uint32_t>{160}, [](const auto& extent, auto key) { return extent.key == key; }));
  }

  SECTION("check backward/forward iterator") {
    constexpr auto kItemsCount = 600 * 300ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
    }

    auto it = bucket.begin();
    uint32_t steps = 0;
    while (it != bucket.end()) {
      CHECK(it->key == steps);
      ++it;
      ++steps;
    }
    CHECK(steps == kItemsCount);
    CHECK(it.is_end());
    while (it != bucket.begin()) {
      --it;
      --steps;
      CHECK(it->key == steps);
    }
    CHECK(steps == 0);
    CHECK(it.is_begin());

    for (int i = 0; i < 40; ++i) {
      ++it;
      ++steps;
      CHECK(it->key == steps);
    }
    for (int i = 0; i < 20; ++i) {
      --it;
      --steps;
      CHECK(it->key == steps);
    }
    CHECK(it->key == 20);
  }
}
