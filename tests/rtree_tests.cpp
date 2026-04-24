/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "rtree.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"
#include "utils/test_utils.h"

namespace {

class RTreeFixture : public MetadataBlockFixture {
 public:
  RTreeFixture() { rtree.Init(/*depth=*/1, /*block_number=*/0); }

  std::shared_ptr<TestBlock> rtree_block = LoadMetadataBlock(0);
  RTree rtree{rtree_block};
};

constexpr int kRTreeItems = 500;

void InsertSequential(RTree& rtree, uint32_t count, uint32_t value = 0) {
  for (uint32_t i = 0; i < count; ++i) {
    REQUIRE(rtree.insert({i, value ? value : i + 1}));
  }
}

}  // namespace

TEST_CASE_METHOD(RTreeFixture, "RTree is empty after initialization", "[rtree][unit]") {
  REQUIRE(rtree.size() == 0);
}

TEST_CASE_METHOD(RTreeFixture,
                 "RTree sorted inserts grow through expected split depths",
                 "[rtree][tree][white-box]") {
  uint32_t index = 0;
  for (int i = 0; i < 4; ++i) {
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;
  }
  REQUIRE(rtree.size() == index);
  REQUIRE(rtree.header()->tree_depth.value() == 0);
  REQUIRE(rtree.begin().parents().size() == 0);
  REQUIRE(rtree.begin().leaf().node.full());

  REQUIRE(rtree.insert({index, index + 1}));
  ++index;

  auto it = rtree.begin();
  REQUIRE(rtree.header()->tree_depth.value() == 1);
  REQUIRE(it.parents().size() == 1);
  REQUIRE(it.parents()[0].node.size() == 2);
  REQUIRE(it.leaf().node.size() == 3);
  for (int i = 0; i < 3; ++i) {
    ++it;
  }
  REQUIRE(it.leaf().node.size() == 2);

  for (int i = 0; i < 3 * 5 - 1; ++i) {
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;
  }
  REQUIRE(rtree.size() == index);
  it = rtree.end();
  REQUIRE(rtree.header()->tree_depth.value() == 1);
  REQUIRE(it.parents()[0].node.full());
  REQUIRE(it.leaf().node.full());

  REQUIRE(rtree.insert({index, index + 1}));
  ++index;
  it = rtree.end();
  REQUIRE(rtree.header()->tree_depth.value() == 2);
  REQUIRE(it.parents().size() == 2);
  REQUIRE(it.parents()[0].node.size() == 2);
  REQUIRE(it.parents()[1].node.size() == 3);
  REQUIRE(it.leaf().node.size() == 2);

  for (int i = 0; i < 3 * 4 * 5 - 1; ++i) {
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;
  }
  REQUIRE(rtree.size() == index);
  it = rtree.end();
  REQUIRE(rtree.header()->tree_depth.value() == 2);
  REQUIRE(it.parents().size() == 2);
  REQUIRE(it.parents()[0].node.full());
  REQUIRE(it.parents()[1].node.full());
  REQUIRE(it.leaf().node.full());

  REQUIRE(rtree.insert({index, index + 1}));
  ++index;

  it = rtree.end();
  REQUIRE(rtree.header()->tree_depth.value() == 3);
  REQUIRE(it.parents().size() == 3);
  REQUIRE(it.parents()[0].node.size() == 2);
  REQUIRE(it.parents()[1].node.size() == 3);
  REQUIRE(it.parents()[2].node.size() == 3);
  REQUIRE(it.leaf().node.size() == 2);

  for (int i = 0; i < 3 * 4 * 4 * 5 - 1; ++i) {
    REQUIRE(rtree.insert({index, index + 1}));
    ++index;
  }
  REQUIRE(rtree.size() == index);
  it = rtree.end();
  REQUIRE(rtree.header()->tree_depth.value() == 3);
  REQUIRE(it.parents().size() == 3);
  REQUIRE(it.parents()[0].node.full());
  REQUIRE(it.parents()[1].node.full());
  REQUIRE(it.parents()[2].node.full());
  REQUIRE(it.leaf().node.full());

  REQUIRE(rtree.insert({index, index + 1}));
  ++index;

  it = rtree.end();
  REQUIRE(rtree.header()->tree_depth.value() == 4);
  REQUIRE(it.parents().size() == 4);
  REQUIRE(it.parents()[0].node.size() == 2);
  REQUIRE(it.parents()[1].node.size() == 3);
  REQUIRE(it.parents()[2].node.size() == 3);
  REQUIRE(it.parents()[3].node.size() == 3);
  REQUIRE(it.leaf().node.size() == 2);

  while (rtree.insert({index, index + 1})) {
    ++index;
  }
  REQUIRE(!rtree.begin().parents()[0].node.full());
  REQUIRE(CollectKeyValues(rtree) == SequentialKeyValues(index));

  for (uint32_t i = 0; i < index; ++i) {
    CAPTURE(i);
    REQUIRE((*rtree.find(i, true)).key() == i);
  }
}

TEST_CASE_METHOD(RTreeFixture, "RTree keeps unsorted inserts ordered", "[rtree][tree][unit]") {
  auto unsorted_keys = createShuffledKeysArray<kRTreeItems>();
  for (auto key : unsorted_keys) {
    REQUIRE(rtree.insert({key, key + 1}));
  }

  auto sorted_keys = unsorted_keys;
  std::ranges::sort(sorted_keys);
  REQUIRE(CollectKeys(rtree) == sorted_keys);
  REQUIRE(CollectKeyValues(rtree) == SequentialKeyValues(kRTreeItems));
}

TEST_CASE_METHOD(RTreeFixture, "RTree erases all sorted items and can be reused", "[rtree][tree][unit]") {
  InsertSequential(rtree, kRTreeItems);
  for (uint32_t i = 0; i < kRTreeItems; ++i) {
    CAPTURE(i);
    REQUIRE(rtree.erase(i));
  }
  REQUIRE(rtree.begin() == rtree.end());
  REQUIRE(rtree.empty());
  REQUIRE(rtree.header()->tree_depth.value() == 0);

  InsertSequential(rtree, kRTreeItems);
  REQUIRE(CollectKeys(rtree) == SequentialKeys(kRTreeItems));
}

TEST_CASE_METHOD(RTreeFixture, "RTree erases shuffled items and collapses empty", "[rtree][tree][unit]") {
  InsertSequential(rtree, kRTreeItems);

  auto unsorted_keys = createShuffledKeysArray<kRTreeItems>();
  auto middle = unsorted_keys.begin() + kRTreeItems / 2;
  for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
    CAPTURE(key);
    REQUIRE(rtree.erase(key));
  }

  auto sorted_upper_half = std::ranges::to<std::vector>(std::ranges::subrange(middle, unsorted_keys.end()));
  std::ranges::sort(sorted_upper_half);
  REQUIRE(CollectKeys(rtree) == sorted_upper_half);

  for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
    CAPTURE(key);
    REQUIRE(rtree.erase(key));
  }

  REQUIRE(rtree.begin() == rtree.end());
  REQUIRE(rtree.empty());
  REQUIRE(rtree.header()->tree_depth.value() == 0);
}
