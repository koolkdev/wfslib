/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "eptree.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"
#include "utils/test_utils.h"

namespace {

class EPTreeFixture : public FreeBlocksAllocatorFixture {
 public:
  EPTreeFixture() {
    allocator_initialized = allocator.Init(1000000);
    eptree.Init(/*block_number=*/0);
  }

  EPTree eptree{&allocator};
  bool allocator_initialized = false;
};

constexpr uint32_t kEPTreeStressItems = 600 * 300;

}  // namespace

TEST_CASE_METHOD(EPTreeFixture, "EPTree inserts sorted extents and grows depth", "[eptree][tree][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kEPTreeStressItems; ++i) {
    REQUIRE(eptree.insert({i, i + 1}));
  }

  REQUIRE(eptree.tree_header()->depth.value() == 3);
  REQUIRE(CollectKeyValues(eptree) == SequentialKeyValues(kEPTreeStressItems));
}

TEST_CASE_METHOD(EPTreeFixture, "EPTree keeps unsorted inserts ordered and searchable", "[eptree][tree][stress]") {
  REQUIRE(allocator_initialized);

  auto unsorted_keys = createShuffledKeysArray<kEPTreeStressItems>();
  for (auto key : unsorted_keys) {
    REQUIRE(eptree.insert({key, key + 1}));
  }

  auto sorted_keys = unsorted_keys;
  std::ranges::sort(sorted_keys);
  REQUIRE(CollectKeys(eptree) == sorted_keys);
  REQUIRE(CollectKeyValues(eptree) == SequentialKeyValues(kEPTreeStressItems));

  for (uint32_t i = 0; i < kEPTreeStressItems; ++i) {
    CAPTURE(i);
    REQUIRE((*eptree.find(i, true)).key() == i);
  }
}

TEST_CASE_METHOD(EPTreeFixture,
                 "EPTree erases shuffled extents and collapses to an empty root",
                 "[eptree][tree][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kEPTreeStressItems; ++i) {
    REQUIRE(eptree.insert({i, 0}));
  }

  auto unsorted_keys = createShuffledKeysArray<kEPTreeStressItems>();
  auto middle = unsorted_keys.begin() + kEPTreeStressItems / 2;
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
    CAPTURE(key);
    REQUIRE(eptree.erase(key, blocks_to_delete));
  }

  auto sorted_upper_half = std::ranges::to<std::vector>(std::ranges::subrange(middle, unsorted_keys.end()));
  std::ranges::sort(sorted_upper_half);
  REQUIRE(CollectKeys(eptree) == sorted_upper_half);

  for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
    CAPTURE(key);
    REQUIRE(eptree.erase(key, blocks_to_delete));
  }

  REQUIRE(eptree.begin() == eptree.end());
  REQUIRE(eptree.tree_header()->depth.value() == 1);
}

TEST_CASE_METHOD(EPTreeFixture, "EPTree iterator walks forward and backward", "[eptree][tree][iterator][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kEPTreeStressItems; ++i) {
    REQUIRE(eptree.insert({i, i}));
  }

  RequireBidirectionalIteration(eptree, kEPTreeStressItems, [](const auto& extent) { return extent.key(); });
}
