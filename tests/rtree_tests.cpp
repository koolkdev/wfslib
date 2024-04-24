/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_range_equals.hpp>
#include <random>
#include <ranges>

#include "../src/rtree.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"
#include "utils/test_utils.h"

using Catch::Matchers::RangeEquals;

TEST_CASE("RTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto rtree_block = TestBlock::LoadMetadataBlock(test_device, 0);
  RTree rtree{rtree_block};
  rtree.Init(/*depth=*/1, /*block_number=*/0);

  SECTION("Check empty rtree size") {
    REQUIRE(rtree.size() == 0);
  }

  SECTION("insert items sorted") {
    auto index = 0ul;
    for (int i = 0; i < 4; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    CHECK(rtree.size() == index);
    // No parent nodes yet
    CHECK(rtree.header()->tree_depth.value() == 0);
    CHECK(rtree.begin().parents().size() == 0);
    CHECK(rtree.begin().leaf().node->full());

    // for (auto [i, extent] : std::views::enumerate(rtree)) {
    //  REQUIRE(extent.key == static_cast<uint32_t>(i));
    //  REQUIRE(extent.value == static_cast<uint32_t>(i + 1));
    //}

    REQUIRE(rtree.insert({index, index + 1}));
    ++index;

    auto it = rtree.begin();
    CHECK(rtree.header()->tree_depth.value() == 1);
    CHECK(it.parents().size() == 1);
    CHECK(it.parents()[0].node->size() == 2);  // the two splitted nodes
    // Should have splitted it to 3/1 + plus the new one
    CHECK(it.leaf().node->size() == 3);
    // Go to next node
    for (int i = 0; i < 3; ++i)
      ++it;
    CHECK(it.leaf().node->size() == 2);

    // Now fill the second one 5 more times
    for (int i = 0; i < 3 * 5 - 1; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    CHECK(rtree.size() == index);
    // Still one parent, but it is full now
    it = rtree.end();
    CHECK(rtree.header()->tree_depth.value() == 1);
    CHECK(it.parents()[0].node->full());
    CHECK(it.leaf().node->full());

    REQUIRE(rtree.insert({index, index + 1}));
    ++index;
    // new parent
    it = rtree.end();
    CHECK(rtree.header()->tree_depth.value() == 2);
    CHECK(it.parents().size() == 2);
    CHECK(it.parents()[0].node->size() == 2);  // the two splitted nodes
    CHECK(it.parents()[1].node->size() == 3);  // should have been splitted 4/2 + the new one
    CHECK(it.leaf().node->size() == 2);

    // Now fill our parent again 4 X 5 times
    for (int i = 0; i < 3 * 4 * 5 - 1; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    REQUIRE(rtree.size() == index);
    it = rtree.end();
    // our parents should be full now
    CHECK(rtree.header()->tree_depth.value() == 2);
    CHECK(it.parents().size() == 2);
    CHECK(it.parents()[0].node->full());
    CHECK(it.parents()[1].node->full());
    CHECK(it.leaf().node->full());

    // Now we should grow by another depth
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;

    it = rtree.end();
    CHECK(rtree.header()->tree_depth.value() == 3);
    CHECK(it.parents().size() == 3);
    CHECK(it.parents()[0].node->size() == 2);  // the two splitted nodes
    CHECK(it.parents()[1].node->size() == 3);  // should have been splitted 4/2
    CHECK(it.parents()[2].node->size() == 3);  // should have been splitted 4/2
    CHECK(it.leaf().node->size() == 2);

    // Now fill our parent again 4 X 4 X 5 times
    for (int i = 0; i < 3 * 4 * 4 * 5 - 1; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    CHECK(rtree.size() == index);
    it = rtree.end();
    // our parents should be full now
    CHECK(rtree.header()->tree_depth.value() == 3);
    CHECK(it.parents().size() == 3);
    CHECK(it.parents()[0].node->full());
    CHECK(it.parents()[1].node->full());
    CHECK(it.parents()[2].node->full());
    CHECK(it.leaf().node->full());

    // Now we should grow by another depth
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;

    it = rtree.end();
    CHECK(rtree.header()->tree_depth.value() == 4);
    CHECK(it.parents().size() == 4);
    CHECK(it.parents()[0].node->size() == 2);  // the two splitted nodes
    CHECK(it.parents()[1].node->size() == 3);  // should have been splitted 4/2
    CHECK(it.parents()[2].node->size() == 3);  // should have been splitted 4/2
    CHECK(it.parents()[3].node->size() == 3);  // should have been splitted 4/2
    CHECK(it.leaf().node->size() == 2);

    // Now fill our tree until full
    while (rtree.insert({index, index + 1})) {
      ++index;
    }
    CHECK(!rtree.begin().parents()[0].node->full());

    CHECK_THAT(rtree, RangeEquals(std::views::iota(0ul, index),
                                  [](const auto& extent, auto i) { return extent.key == i && extent.value == i + 1; }));

    for (auto i = 0ul; i < index; ++i) {
      CHECK(rtree.find(i, true)->key == i);
    }
  }

  SECTION("insert items unsorted") {
    constexpr auto kItemsCount = 500ul;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(rtree.insert({key, key + 1}));
    }

    // Check that the tree is sorted
    auto keys = std::views::transform(rtree, [](const auto& extent) -> uint32_t { return extent.key; });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    CHECK_THAT(keys, RangeEquals(sorted_keys));

    CHECK_THAT(rtree, RangeEquals(std::views::iota(0ul, kItemsCount),
                                  [](const auto& extent, auto i) { return extent.key == i && extent.value == i + 1; }));
  }

  SECTION("erase items after inserting") {
    constexpr auto kItemsCount = 500ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.insert({i, 0}));
    }
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.erase(i));
    }
    CHECK(rtree.begin() == rtree.end());
    CHECK(rtree.empty());
    CHECK(rtree.header()->tree_depth.value() == 0);

    // Check that we can insert items again
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.insert({i, 0}));
    }
    CHECK_THAT(rtree, RangeEquals(std::views::iota(0ul, kItemsCount),
                                  [](const auto& extent, auto i) { return extent.key == i; }));
  }

  SECTION("erase items randomly") {
    constexpr auto kItemsCount = 500ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.insert({i, 0}));
    }
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    auto middle = unsorted_keys.begin() + kItemsCount / 2;
    // Remove half the items first
    for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
      REQUIRE(rtree.erase(key));
    }

    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_keys.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    // Ensure that the right items were deleted
    CHECK_THAT(rtree, RangeEquals(sorted_upper_half, [](const auto& extent, auto key) { return extent.key == key; }));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(rtree.erase(key));
    }

    // Should be empty
    CHECK(rtree.begin() == rtree.end());
    CHECK(rtree.empty());
    CHECK(rtree.header()->tree_depth.value() == 0);
  }
}
