/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "free_blocks_tree.h"
#include "free_blocks_tree_bucket.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"
#include "utils/test_utils.h"

TEST_CASE("FreeBlocksTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto allocator_block = TestBlock::LoadMetadataBlock(test_device, 0);
  TestFreeBlocksAllocator allocator{allocator_block, test_device};
  REQUIRE(allocator.Init(1000000));

  FreeBlocksTree tree{&allocator};

  SECTION("iterate empty tree") {
    REQUIRE(std::distance(tree.begin(), tree.end()) == 0);
  }

  SECTION("insert items sorted") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(FreeBlocksTreeBucket{&allocator, i % kSizeBuckets.size()}.insert({i, static_cast<nibble>(i % 16)}));
    }

    REQUIRE(std::ranges::equal(
        std::views::transform(tree,
                              [](const auto& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key(), extent.value(), extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBuckets.size())};
        })));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE((*tree.find(i)).key() == i);
    }
  }

  SECTION("insert items unsorted") {
    constexpr int kItemsCount = 600 * 300;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(FreeBlocksTreeBucket{&allocator, key % kSizeBuckets.size()}.insert({key, static_cast<nibble>(key % 16)}));
    }

    // Check that the tree is sorted
    auto keys = std::views::transform(tree, [](const auto& extent) -> uint32_t { return extent.key(); });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    REQUIRE(std::ranges::equal(sorted_keys, keys));

    // Check the values
    REQUIRE(std::ranges::equal(
        std::views::transform(tree,
                              [](const auto& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key(), extent.value(), extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBuckets.size())};
        })));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE((*tree.find(i)).key() == i);
    }
  }

  SECTION("erase items randomly") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(FreeBlocksTreeBucket{&allocator, i % kSizeBuckets.size()}.insert({i, static_cast<nibble>(i % 16)}));
    }
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    auto middle = unsorted_keys.begin() + kItemsCount / 2;
    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    // Remove half the items first
    for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
      REQUIRE(FreeBlocksTreeBucket{&allocator, key % kSizeBuckets.size()}.erase(key, blocks_to_delete));
    }

    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_keys.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    // Ensure that the right items were deleted
    REQUIRE(std::ranges::equal(std::views::transform(tree, [](const auto& extent) -> int { return extent.key(); }),
                               sorted_upper_half));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(FreeBlocksTreeBucket{&allocator, key % kSizeBuckets.size()}.erase(key, blocks_to_delete));
    }

    // Should be empty
    REQUIRE(tree.begin() == tree.end());

    auto blocks_numbers_to_delete =
        std::views::transform(blocks_to_delete, [](const FreeBlocksRangeInfo& range) { return range.block_number; }) |
        std::ranges::to<std::vector>();

    std::ranges::sort(blocks_numbers_to_delete);
    // Check deleted blocks, everything beside first ftree should be deleted
    REQUIRE(std::ranges::equal(blocks_numbers_to_delete, std::views::iota(allocator.initial_frees_block_number(),
                                                                          allocator.AllocFreeBlockFromCache())));
  }

  SECTION("empty ftree find") {
    EPTree eptree{&allocator};
    // Have three empty ftrees, check that items areadded to the correct ones, and check how find works with them.
    FreeBlocksTreeBucket bucket{&allocator, 3};
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

    REQUIRE(ftree1.empty());
    REQUIRE(ftree2.empty());
    REQUIRE(ftree3.empty());
    REQUIRE(tree.find(150, false) == tree.end());

    // insert to first ftree
    REQUIRE(bucket.insert({50, nibble{0}}));
    REQUIRE(ftree1.size() == 1);
    REQUIRE(ftree2.empty());
    REQUIRE(ftree3.empty());
    REQUIRE(tree.find(150, false) != tree.end());
    REQUIRE((*tree.find(150, false)).key() == 50);
    REQUIRE((*tree.find(25, false)).key() == 50);
    REQUIRE(std::ranges::equal(std::views::transform(tree, [](const auto& extent) -> int { return extent.key(); }),
                               std::list<uint32_t>{50}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(tree), [](const auto& extent) -> int { return extent.key(); }),
        std::list<uint32_t>{50}));

    REQUIRE(bucket.insert({160, nibble{0}}));
    REQUIRE(ftree1.size() == 1);
    REQUIRE(ftree2.size() == 1);
    REQUIRE(ftree3.empty());
    REQUIRE((*tree.find(150, false)).key() == 50);
    REQUIRE((*tree.find(250, false)).key() == 160);
    REQUIRE(std::ranges::equal(std::views::transform(tree, [](const auto& extent) -> int { return extent.key(); }),
                               std::list<uint32_t>{50, 160}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(tree), [](const auto& extent) -> int { return extent.key(); }),
        std::list<uint32_t>{160, 50}));

    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    REQUIRE(bucket.erase(50, blocks_to_delete));
    REQUIRE(blocks_to_delete.empty());  // first block should delete it
    REQUIRE(ftree1.empty());
    REQUIRE(ftree2.size() == 1);
    REQUIRE(ftree3.empty());
    REQUIRE((*tree.find(150, false)).key() == 160);
    REQUIRE((*tree.find(250, false)).key() == 160);
    REQUIRE((*tree.find(25, false)).key() == 160);
    REQUIRE(std::ranges::equal(std::views::transform(tree, [](const auto& extent) -> int { return extent.key(); }),
                               std::list<uint32_t>{160}));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(tree), [](const auto& extent) -> int { return extent.key(); }),
        std::list<uint32_t>{160}));
  }

  SECTION("check backward/forward iterator") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(FreeBlocksTreeBucket{&allocator, i % kSizeBuckets.size()}.insert({i, static_cast<nibble>(i % 16)}));
    }

    auto it = tree.begin();
    uint32_t steps = 0;
    while (it != tree.end()) {
      REQUIRE((*it).key() == steps);
      ++it;
      ++steps;
    }
    REQUIRE(steps == kItemsCount);
    REQUIRE(it.is_end());
    while (it != tree.begin()) {
      --it;
      --steps;
      REQUIRE((*it).key() == steps);
    }
    REQUIRE(steps == 0);
    REQUIRE(it.is_begin());

    for (int i = 0; i < 40; ++i) {
      ++it;
      ++steps;
      REQUIRE((*it).key() == steps);
    }
    for (int i = 0; i < 20; ++i) {
      --it;
      --steps;
      REQUIRE((*it).key() == steps);
    }
    REQUIRE((*it).key() == 20);
  }
}
