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

#include "../src/eptree.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"
#include "utils/test_utils.h"

using Catch::Matchers::RangeEquals;

TEST_CASE("EPTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto allocator_block = TestBlock::LoadMetadataBlock(test_device, 0);
  TestFreeBlocksAllocator allocator{allocator_block, test_device};
  allocator.Init(1000000);
  EPTree eptree{&allocator};
  eptree.Init(/*block_number=*/0);

  SECTION("insert items sorted") {
    constexpr auto kItemsCount = 600 * 300ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(eptree.insert({i, i + 1}));
    }

    CHECK(eptree.tree_header()->depth.value() == 3);
    CHECK_THAT(eptree, RangeEquals(std::views::iota(0ul, kItemsCount), [](const auto& extent, uint32_t i) {
                 return extent.key == i && extent.value == i + 1;
               }));
  }

  SECTION("insert items unsorted") {
    constexpr auto kItemsCount = 600 * 300ul;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(eptree.insert({key, key + 1}));
    }

    // Check that the tree is sorted
    auto keys = std::views::transform(eptree, [](const auto& extent) -> uint32_t { return extent.key; });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    CHECK_THAT(keys, RangeEquals(sorted_keys));

    // Check the values
    CHECK_THAT(eptree, RangeEquals(std::views::iota(0ul, kItemsCount), [](const auto& extent, uint32_t i) {
                 return extent.key == i && extent.value == i + 1;
               }));

    for (uint32_t i = 0; i < kItemsCount; ++i) {
      CHECK(eptree.find(i, true)->key == i);
    }
  }

  SECTION("erase items randomly") {
    constexpr auto kItemsCount = 600 * 300ul;
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

    // Ensure that the right items were deleted
    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_keys.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    CHECK_THAT(eptree, RangeEquals(sorted_upper_half, [](const auto& extent, auto key) { return extent.key == key; }));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(eptree.erase(key, blocks_to_delete));
    }

    // Should be empty
    CHECK(eptree.begin() == eptree.end());
    CHECK(eptree.tree_header()->depth.value() == 1);
  }

  SECTION("check backward/forward iterator") {
    constexpr auto kItemsCount = 600 * 300ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(eptree.insert({i, i}));
    }

    auto it = eptree.begin();
    uint32_t steps = 0;
    while (it != eptree.end()) {
      CHECK(it->key == steps);
      ++it;
      ++steps;
    }
    CHECK(steps == kItemsCount);
    CHECK(it.is_end());
    while (it != eptree.begin()) {
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
