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

#include "../src/eptree.h"

#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"
#include "test_metadata_block.h"
#include "test_utils.h"

TEST_CASE("EPTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto allocator_block = TestMetadataBlock::LoadBlock(test_device, 0);
  TestFreeBlocksAllocator allocator{allocator_block, test_device};
  allocator.Init(1000000);
  EPTree eptree{&allocator};
  eptree.Init();

  SECTION("insert items sorted") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(eptree.insert({i, i + 1}));
    }

    REQUIRE(eptree.tree_header()->depth.value() == 3);

    REQUIRE(std::ranges::equal(
        std::views::transform(eptree,
                              [](const RTree::iterator::value_type& extent) -> std::pair<uint32_t, uint32_t> {
                                return {extent.key, extent.value};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::pair<uint32_t, uint32_t> {
          return {i, i + 1};
        })));
  }

  SECTION("insert items unsorted") {
    constexpr int kItemsCount = 600 * 300;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(eptree.insert({key, key + 1}));
    }

    // Check that the tree is sorted
    auto keys =
        std::views::transform(eptree, [](const RTree::iterator::value_type& extent) -> uint32_t { return extent.key; });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    REQUIRE(std::ranges::equal(sorted_keys, keys));

    // Check the values
    REQUIRE(std::ranges::equal(
        std::views::transform(eptree,
                              [](const RTree::iterator::value_type& extent) -> std::pair<uint32_t, uint32_t> {
                                return {extent.key, extent.value};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::pair<uint32_t, uint32_t> {
          return {i, i + 1};
        })));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(eptree.find(i, true)->key == i);
    }
  }

  SECTION("erase items randomly") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(eptree.insert({i, 0}));
    }
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    auto middle = unsorted_keys.begin() + kItemsCount / 2;
    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    // Remove half the items first
    for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
      REQUIRE(eptree.erase(key, blocks_to_delete));
    }

    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_keys.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    // Ensure that the right items were deleted
    REQUIRE(std::ranges::equal(
        std::views::transform(eptree, [](const RTree::iterator::value_type& extent) -> int { return extent.key; }),
        sorted_upper_half));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(eptree.erase(key, blocks_to_delete));
    }

    // Should be empty
    REQUIRE(eptree.begin() == eptree.end());
    REQUIRE(eptree.tree_header()->depth.value() == 1);
  }

  SECTION("check backward/forward iterator") {
    constexpr int kItemsCount = 600 * 300;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(eptree.insert({i, i}));
    }

    auto it = eptree.begin();
    uint32_t steps = 0;
    while (it != eptree.end()) {
      REQUIRE(it->key == steps);
      ++it;
      ++steps;
    }
    REQUIRE(steps == kItemsCount);
    REQUIRE(it.is_end());
    while (it != eptree.begin()) {
      --it;
      --steps;
      REQUIRE(it->key == steps);
    }
    REQUIRE(steps == 0);
    REQUIRE(it.is_begin());

    for (int i = 0; i < 40; ++i) {
      ++it;
      ++steps;
      REQUIRE(it->key == steps);
    }
    for (int i = 0; i < 20; ++i) {
      --it;
      --steps;
      REQUIRE(it->key == steps);
    }
    REQUIRE(it->key == 20);
  }
}
