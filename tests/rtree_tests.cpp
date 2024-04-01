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

#include "../src/rtree.h"

#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"
#include "test_metadata_block.h"
#include "test_utils.h"

TEST_CASE("RTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto rtree_block = TestMetadataBlock::LoadBlock(test_device, 0);
  RTree rtree{rtree_block};
  rtree.Init(/*depth=*/1);

  SECTION("Check empty rtree size") {
    REQUIRE(rtree.size() == 0);
  }

  SECTION("insert items sorted") {
    uint32_t index = 0;
    for (int i = 0; i < 4; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    REQUIRE(rtree.size() == index);
    // No parent nodes yet
    REQUIRE(rtree.header()->tree_depth.value() == 0);
    REQUIRE(rtree.begin().parents().size() == 0);
    REQUIRE(rtree.begin().leaf().node->full());

    // for (auto [i, extent] : std::views::enumerate(rtree)) {
    //  REQUIRE(extent.key == static_cast<uint32_t>(i));
    //  REQUIRE(extent.value == static_cast<uint32_t>(i + 1));
    //}

    REQUIRE(rtree.insert({index, index + 1}));
    ++index;

    auto it = rtree.begin();
    REQUIRE(rtree.header()->tree_depth.value() == 1);
    REQUIRE(it.parents().size() == 1);
    REQUIRE(it.parents()[0].node->size() == 2);  // the two splitted nodes
    // Should have splitted it to 3/1 + plus the new one
    REQUIRE(it.leaf().node->size() == 3);
    // Go to next node
    for (int i = 0; i < 3; ++i)
      ++it;
    REQUIRE(it.leaf().node->size() == 2);

    // Now fill the second one 5 more times
    for (int i = 0; i < 3 * 5 - 1; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    REQUIRE(rtree.size() == index);
    // Still one parent, but it is full now
    it = rtree.end();
    REQUIRE(rtree.header()->tree_depth.value() == 1);
    REQUIRE(it.parents()[0].node->full());
    REQUIRE(it.leaf().node->full());

    REQUIRE(rtree.insert({index, index + 1}));
    ++index;
    // new parent
    it = rtree.end();
    REQUIRE(rtree.header()->tree_depth.value() == 2);
    REQUIRE(it.parents().size() == 2);
    REQUIRE(it.parents()[0].node->size() == 2);  // the two splitted nodes
    REQUIRE(it.parents()[1].node->size() == 3);  // should have been splitted 4/2 + the new one
    REQUIRE(it.leaf().node->size() == 2);

    // Now fill our parent again 4 X 5 times
    for (int i = 0; i < 3 * 4 * 5 - 1; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    REQUIRE(rtree.size() == index);
    it = rtree.end();
    // our parents should be full now
    REQUIRE(rtree.header()->tree_depth.value() == 2);
    REQUIRE(it.parents().size() == 2);
    REQUIRE(it.parents()[0].node->full());
    REQUIRE(it.parents()[1].node->full());
    REQUIRE(it.leaf().node->full());

    // Now we should grow by another depth
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;

    it = rtree.end();
    REQUIRE(rtree.header()->tree_depth.value() == 3);
    REQUIRE(it.parents().size() == 3);
    REQUIRE(it.parents()[0].node->size() == 2);  // the two splitted nodes
    REQUIRE(it.parents()[1].node->size() == 3);  // should have been splitted 4/2
    REQUIRE(it.parents()[2].node->size() == 3);  // should have been splitted 4/2
    REQUIRE(it.leaf().node->size() == 2);

    // Now fill our parent again 4 X 4 X 5 times
    for (int i = 0; i < 3 * 4 * 4 * 5 - 1; ++i) {
      REQUIRE(rtree.insert({index, index + 1}));
      ++index;
    }
    REQUIRE(rtree.size() == index);
    it = rtree.end();
    // our parents should be full now
    REQUIRE(rtree.header()->tree_depth.value() == 3);
    REQUIRE(it.parents().size() == 3);
    REQUIRE(it.parents()[0].node->full());
    REQUIRE(it.parents()[1].node->full());
    REQUIRE(it.parents()[2].node->full());
    REQUIRE(it.leaf().node->full());

    // Now we should grow by another depth
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;

    it = rtree.end();
    REQUIRE(rtree.header()->tree_depth.value() == 4);
    REQUIRE(it.parents().size() == 4);
    REQUIRE(it.parents()[0].node->size() == 2);  // the two splitted nodes
    REQUIRE(it.parents()[1].node->size() == 3);  // should have been splitted 4/2
    REQUIRE(it.parents()[2].node->size() == 3);  // should have been splitted 4/2
    REQUIRE(it.parents()[3].node->size() == 3);  // should have been splitted 4/2
    REQUIRE(it.leaf().node->size() == 2);

    // Now fill our tree until full
    while (rtree.insert({index, index + 1})) {
      ++index;
    }
    REQUIRE(!rtree.begin().parents()[0].node->full());

    REQUIRE(std::ranges::equal(
        std::views::transform(rtree,
                              [](const RTree::iterator::value_type& extent) -> std::pair<uint32_t, uint32_t> {
                                return {extent.key, extent.value};
                              }),
        std::views::transform(std::views::iota(0, static_cast<int>(index)), [](int i) -> std::pair<uint32_t, uint32_t> {
          return {i, i + 1};
        })));

    for (uint32_t i = 0; i < index; ++i) {
      REQUIRE(rtree.find(i, true)->key == i);
    }
  }

  SECTION("insert items unsorted") {
    constexpr int kItemsCount = 500;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto key : unsorted_keys) {
      REQUIRE(rtree.insert({key, key + 1}));
    }

    // Check that the tree is sorted
    auto keys =
        std::views::transform(rtree, [](const RTree::iterator::value_type& extent) -> uint32_t { return extent.key; });
    auto sorted_keys = unsorted_keys;
    std::ranges::sort(sorted_keys);
    REQUIRE(std::ranges::equal(keys, sorted_keys));

    REQUIRE(std::ranges::equal(
        std::views::transform(rtree,
                              [](const RTree::iterator::value_type& extent) -> std::pair<uint32_t, uint32_t> {
                                return {extent.key, extent.value};
                              }),
        std::views::transform(std::views::iota(0, static_cast<int>(kItemsCount)),
                              [](int i) -> std::pair<uint32_t, uint32_t> {
                                return {i, i + 1};
                              })));
  }

  SECTION("erase items after inserting") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.insert({i, 0}));
    }
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.erase(i));
    }
    REQUIRE(rtree.begin() == rtree.end());
    REQUIRE(rtree.empty());
    REQUIRE(rtree.header()->tree_depth.value() == 0);

    // Check that we can insert items again
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(rtree.insert({i, 0}));
    }
    REQUIRE(std::ranges::equal(
        std::views::transform(rtree, [](const RTree::iterator::value_type& extent) -> int { return extent.key; }),
        std::ranges::iota_view(0, kItemsCount)));
  }

  SECTION("erase items randomly") {
    constexpr int kItemsCount = 500;
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
    REQUIRE(std::ranges::equal(
        std::views::transform(rtree, [](const RTree::iterator::value_type& extent) -> int { return extent.key; }),
        sorted_upper_half));

    // Remove the second half
    for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
      REQUIRE(rtree.erase(key));
    }

    // Should be empty
    REQUIRE(rtree.begin() == rtree.end());
    REQUIRE(rtree.empty());
    REQUIRE(rtree.header()->tree_depth.value() == 0);
  }
}
