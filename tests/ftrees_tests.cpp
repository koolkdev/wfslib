/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>

#include "ftrees.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"

namespace {

class FTreesFixture : public MetadataBlockFixture {
 public:
  FTreesFixture() { ftrees.Init(); }

  std::shared_ptr<TestBlock> ftrees_block = LoadMetadataBlock(0);
  FTrees ftrees{ftrees_block};
};

constexpr int kFTreesItems = 500;

std::vector<std::tuple<uint32_t, nibble, size_t>> ExpectedFTreesExtents(uint32_t count) {
  std::vector<std::tuple<uint32_t, nibble, size_t>> values;
  values.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    values.emplace_back(i, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBuckets.size()));
  }
  return values;
}

std::vector<std::tuple<uint32_t, nibble, size_t>> ExpectedEvenKeyExtents(uint32_t begin, uint32_t end) {
  std::vector<std::tuple<uint32_t, nibble, size_t>> values;
  values.reserve(end - begin);
  for (uint32_t i = begin; i < end; ++i) {
    values.emplace_back(i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBuckets.size()));
  }
  return values;
}

void InsertRoundRobin(FTrees& ftrees, uint32_t count, size_t buckets_count = kSizeBuckets.size()) {
  for (uint32_t i = 0; i < count; ++i) {
    REQUIRE(ftrees.ftrees()[i % buckets_count].insert({i, static_cast<nibble>(i % 16)}));
  }
}

}  // namespace

TEST_CASE_METHOD(FTreesFixture, "FTrees is empty after initialization", "[ftrees][unit]") {
  REQUIRE(ftrees.empty());
}

TEST_CASE_METHOD(FTreesFixture, "FTrees inserts sorted extents across buckets", "[ftrees][unit]") {
  InsertRoundRobin(ftrees, kFTreesItems);

  REQUIRE(!ftrees.empty());
  REQUIRE(ftrees.size() == kFTreesItems);
  REQUIRE(CollectFreeExtents(ftrees) == ExpectedFTreesExtents(kFTreesItems));
}

TEST_CASE_METHOD(FTreesFixture, "FTrees reverse iterator walks descending extents", "[ftrees][iterator][unit]") {
  InsertRoundRobin(ftrees, kFTreesItems);

  auto expected = ExpectedFTreesExtents(kFTreesItems);
  std::ranges::reverse(expected);
  REQUIRE(CollectFreeExtents(std::views::reverse(ftrees)) == expected);
}

TEST_CASE_METHOD(FTreesFixture, "FTrees iterator walks forward and backward", "[ftrees][iterator][unit]") {
  InsertRoundRobin(ftrees, kFTreesItems, kSizeBuckets.size() - 2);

  RequireBidirectionalIteration(ftrees, kFTreesItems, [](const auto& extent) { return extent.key(); });
}

TEST_CASE_METHOD(FTreesFixture, "FTrees find returns exact and nearest extents", "[ftrees][unit]") {
  for (uint32_t i = 0; i < kFTreesItems; ++i) {
    REQUIRE(ftrees.ftrees()[i % kSizeBuckets.size()].insert({i * 2, static_cast<nibble>(i % 16)}));
  }

  auto it = ftrees.find(523);
  REQUIRE(it.is_end());

  it = ftrees.find(523, false);
  REQUIRE((*it).key() == 522);
  REQUIRE(CollectFreeExtents(std::ranges::subrange(ftrees.begin(), it)) == ExpectedEvenKeyExtents(0, 522 / 2));
  REQUIRE(CollectFreeExtents(std::ranges::subrange(it, ftrees.end())) ==
          ExpectedEvenKeyExtents(522 / 2, kFTreesItems));

  it = ftrees.find(840);
  REQUIRE((*it).key() == 840);
  REQUIRE(CollectFreeExtents(std::ranges::subrange(ftrees.begin(), it)) == ExpectedEvenKeyExtents(0, 840 / 2));
  REQUIRE(CollectFreeExtents(std::ranges::subrange(it, ftrees.end())) ==
          ExpectedEvenKeyExtents(840 / 2, kFTreesItems));

  REQUIRE((*ftrees.find(4, false)).key() == 4);
  REQUIRE((*ftrees.find(6, false)).key() == 6);
  REQUIRE((*++ftrees.find(4, false)).key() == 6);
  REQUIRE((*++ftrees.find(14, false)).key() == 16);
}

TEST_CASE_METHOD(FTreesFixture, "FTrees split preserves both halves", "[ftrees][unit]") {
  InsertRoundRobin(ftrees, kFTreesItems);

  key_type split_point;
  std::array<FTrees, 2> new_ftrees{FTrees{LoadMetadataBlock(1)}, FTrees{LoadMetadataBlock(2)}};
  new_ftrees[0].Init();
  new_ftrees[1].Init();
  ftrees.split(new_ftrees[0], new_ftrees[1], split_point);

  REQUIRE(split_point == 252);
  REQUIRE(new_ftrees[0].size() == split_point);
  REQUIRE(new_ftrees[1].size() == kFTreesItems - split_point);
  REQUIRE(std::ranges::size(new_ftrees[0]) == split_point);
  REQUIRE(std::ranges::size(new_ftrees[1]) == kFTreesItems - split_point);
  REQUIRE(std::ranges::distance(new_ftrees[0].begin(), new_ftrees[0].end()) == static_cast<int>(split_point));
  REQUIRE(std::ranges::distance(new_ftrees[1].begin(), new_ftrees[1].end()) ==
          static_cast<int>(kFTreesItems - split_point));
  REQUIRE(std::ranges::equal(std::ranges::subrange(ftrees.begin(), ftrees.find(split_point)), new_ftrees[0]));
  REQUIRE(std::ranges::equal(std::ranges::subrange(ftrees.find(split_point), ftrees.end()), new_ftrees[1]));
}

TEST_CASE_METHOD(FTreesFixture, "FTrees split chooses the middle of the largest bucket", "[ftrees][unit]") {
  for (uint32_t i = 0; i < kFTreesItems; ++i) {
    REQUIRE(ftrees.ftrees()[5].insert({i, static_cast<nibble>(i % 16)}));
  }
  REQUIRE(ftrees.ftrees()[4].insert({700, nibble{0}}));
  REQUIRE(ftrees.ftrees()[6].insert({800, nibble{0}}));
  REQUIRE(ftrees.ftrees()[2].insert({1000, nibble{0}}));

  key_type split_point;
  std::array<FTrees, 2> new_ftrees{FTrees{LoadMetadataBlock(1)}, FTrees{LoadMetadataBlock(2)}};
  new_ftrees[0].Init();
  new_ftrees[1].Init();
  ftrees.split(new_ftrees[0], new_ftrees[1], split_point);

  REQUIRE(split_point == 250);
}
