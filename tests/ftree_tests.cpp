/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "ftree.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"

namespace {

class FTreeFixture : public MetadataBlockFixture {
 public:
  FTreeFixture() {
    FTreesBlock{ftrees_block}.Init();
    ftrees.reserve(kSizeBuckets.size());
    for (size_t i = 0; i < kSizeBuckets.size(); ++i) {
      ftrees.emplace_back(ftrees_block, i);
    }
  }

  std::shared_ptr<TestBlock> ftrees_block = LoadMetadataBlock(0);
  std::vector<FTree> ftrees;
};

constexpr int kFTreeItems = 500;

auto CollectFTreeValues(FTree& ftree) {
  return CollectRange(ftree,
                      [](const auto& extent) -> std::pair<uint32_t, nibble> { return {extent.key(), extent.value()}; });
}

}  // namespace

TEST_CASE_METHOD(FTreeFixture, "FTree buckets are empty after initialization", "[ftree][unit]") {
  for (size_t i = 0; i < kSizeBuckets.size(); ++i) {
    REQUIRE(ftrees[i].size() == 0);
  }
}

TEST_CASE_METHOD(FTreeFixture, "FTree erases data and frees space for another bucket", "[ftree][unit]") {
  for (uint32_t i = 0; i < kFTreeItems; ++i) {
    REQUIRE(ftrees[0].insert({i, static_cast<nibble>(i % 16)}));
  }

  uint32_t inserted = 0;
  for (uint32_t i = 0; i < kFTreeItems; ++i) {
    inserted += ftrees[1].insert({i, static_cast<nibble>(i % 16)}) ? 1 : 0;
  }
  REQUIRE(inserted < kFTreeItems);
  REQUIRE_FALSE(ftrees[2].insert({0, nibble{0}}));

  REQUIRE(ftrees[0].size() == kFTreeItems);
  REQUIRE(ftrees[1].size() == inserted);
  REQUIRE(ftrees[2].size() == 0);
  REQUIRE(CollectFTreeValues(ftrees[0]) == SequentialNibbleValues(kFTreeItems));

  auto expected_inserted = SequentialNibbleValues(inserted);
  REQUIRE(CollectFTreeValues(ftrees[1]) == expected_inserted);

  ftrees[0].erase(ftrees[0].begin(), ftrees[0].end());
  REQUIRE(ftrees[0].size() == 0);

  for (uint32_t i = 0; i < kFTreeItems; ++i) {
    REQUIRE(ftrees[2].insert({i, static_cast<nibble>(i % 16)}));
  }
  REQUIRE(ftrees[2].size() == kFTreeItems);
}

TEST_CASE_METHOD(FTreeFixture, "FTree compact insert produces an equivalent tree", "[ftree][unit]") {
  for (uint32_t i = 0; i < kFTreeItems; ++i) {
    REQUIRE(ftrees[0].insert({i, static_cast<nibble>(i % 16)}));
  }
  REQUIRE(ftrees[0].size() == kFTreeItems);

  FTree ftree{LoadMetadataBlock(1), 0};
  ftree.Init();
  REQUIRE(ftree.insert_compact(ftrees[0].begin(), ftrees[0].end()));

  REQUIRE(ftree.size() == kFTreeItems);
  REQUIRE(std::distance(ftree.begin(), ftree.end()) == kFTreeItems);
  REQUIRE(std::ranges::equal(ftrees[0], ftree));
  for (uint32_t i = 0; i < kFTreeItems; ++i) {
    CAPTURE(i);
    REQUIRE((*ftree.find(i)).key() == i);
    REQUIRE((*ftree.find(i)).value() == static_cast<nibble>(i % 16));
  }
}

TEST_CASE_METHOD(FTreeFixture, "FTree iterator walks forward and backward", "[ftree][iterator][unit]") {
  for (uint32_t i = 0; i < kFTreeItems; ++i) {
    REQUIRE(ftrees[0].insert({i, static_cast<nibble>(i % 16)}));
  }

  RequireBidirectionalIteration(ftrees[0], kFTreeItems, [](const auto& extent) { return extent.key(); }, 44, 22);
}
