/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_range_equals.hpp>

#include <algorithm>
#include <ranges>

#include "../src/ftrees.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"
#include "utils/test_utils.h"

using Catch::Matchers::RangeEquals;

TEST_CASE("FTreesTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto ftrees_block = TestBlock::LoadMetadataBlock(test_device, 0);
  FTrees ftrees{ftrees_block};
  ftrees.Init();

  SECTION("Check empty ftrees empty") {
    REQUIRE(ftrees.empty());
  }

  SECTION("insert sorted items") {
    constexpr auto kItemsCount = 500ul;
    for (auto i = 0ul; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBuckets.size()].insert({i, static_cast<nibble>(i % 16)}));
    }
    REQUIRE(!ftrees.empty());
    REQUIRE(ftrees.size() == kItemsCount);

    CHECK_THAT(ftrees, RangeEquals(std::views::iota(0ul, kItemsCount), [](const auto& extent, auto i) {
                 return extent.key == i && extent.value == static_cast<nibble>(i % 16) &&
                        extent.bucket_index == i % kSizeBuckets.size();
               }));
  }

  SECTION("check backward iterator") {
    constexpr auto kItemsCount = 500ul;
    for (auto i = 0ul; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBuckets.size()].insert({i, static_cast<nibble>(i % 16)}));
    }

    CHECK_THAT(std::views::reverse(ftrees),
               RangeEquals(std::views::reverse(std::views::iota(0ul, kItemsCount)), [](const auto& extent, auto i) {
                 return extent.key == i && extent.value == static_cast<nibble>(i % 16) &&
                        extent.bucket_index == i % kSizeBuckets.size();
               }));
  }

  SECTION("check backward/forward iterator") {
    constexpr auto kItemsCount = 500ul;
    for (auto i = 0ul; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % (kSizeBuckets.size() - 2)].insert({i, static_cast<nibble>(i % 16)}));
    }

    auto it = ftrees.begin();
    uint32_t steps = 0;
    while (it != ftrees.end()) {
      CHECK(it->key == steps);
      ++it;
      ++steps;
    }
    CHECK(steps == kItemsCount);
    CHECK(it.is_end());
    while (it != ftrees.begin()) {
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

  SECTION("check find") {
    constexpr auto kItemsCount = 500ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBuckets.size()].insert({i * 2, static_cast<nibble>(i % 16)}));
    }

    auto it = ftrees.find(523);
    CHECK(it.is_end());

    it = ftrees.find(523, false);
    CHECK(it->key == 522);

    CHECK_THAT(std::ranges::subrange(ftrees.begin(), it),
               RangeEquals(std::views::iota(0ul, 522ul / 2), [](const auto& extent, auto i) {
                 return extent.key == i * 2 && extent.value == static_cast<nibble>(i % 16) &&
                        extent.bucket_index == i % kSizeBuckets.size();
               }));
    CHECK_THAT(std::ranges::subrange(it, ftrees.end()),
               RangeEquals(std::views::iota(522ul / 2, kItemsCount), [](const auto& extent, auto i) {
                 return extent.key == i * 2 && extent.value == static_cast<nibble>(i % 16) &&
                        extent.bucket_index == i % kSizeBuckets.size();
               }));

    it = ftrees.find(840);
    CHECK(it->key == 840);

    CHECK_THAT(std::ranges::subrange(ftrees.begin(), it),
               RangeEquals(std::views::iota(0ul, 840ul / 2), [](const auto& extent, auto i) {
                 return extent.key == i * 2 && extent.value == static_cast<nibble>(i % 16) &&
                        extent.bucket_index == i % kSizeBuckets.size();
               }));
    CHECK_THAT(std::ranges::subrange(it, ftrees.end()),
               RangeEquals(std::views::iota(840ul / 2, kItemsCount), [](const auto& extent, auto i) {
                 return extent.key == i * 2 && extent.value == static_cast<nibble>(i % 16) &&
                        extent.bucket_index == i % kSizeBuckets.size();
               }));

    CHECK(ftrees.find(4, false)->key == 4);
    CHECK(ftrees.find(6, false)->key == 6);
    CHECK((++ftrees.find(4, false))->key == 6);
    CHECK((++ftrees.find(14, false))->key == 16);
  }

  SECTION("test split") {
    constexpr auto kItemsCount = 500ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBuckets.size()].insert({i, static_cast<nibble>(i % 16)}));
    }

    key_type split_point;
    std::array<FTrees, 2> new_ftrees{FTrees{TestBlock::LoadMetadataBlock(test_device, 1)},
                                     FTrees{TestBlock::LoadMetadataBlock(test_device, 2)}};
    new_ftrees[0].Init();
    new_ftrees[1].Init();
    ftrees.split(new_ftrees[0], new_ftrees[1], split_point);

    // Should be the middle key of the ftree with most items, so somewhere around the actual center in our case
    CHECK(split_point == 252);

    CHECK(new_ftrees[0].size() == split_point);
    CHECK(new_ftrees[1].size() == kItemsCount - split_point);

    CHECK(std::ranges::size(new_ftrees[0]) == split_point);
    CHECK(std::ranges::size(new_ftrees[1]) == kItemsCount - split_point);

    CHECK(std::ranges::distance(new_ftrees[0].begin(), new_ftrees[0].end()) == static_cast<int>(split_point));
    CHECK(std::ranges::distance(new_ftrees[1].begin(), new_ftrees[1].end()) ==
          static_cast<int>(kItemsCount - split_point));

    CHECK_THAT(std::ranges::subrange(ftrees.begin(), ftrees.find(split_point)), RangeEquals(new_ftrees[0]));
    CHECK_THAT(std::ranges::subrange(ftrees.find(split_point), ftrees.end()), RangeEquals(new_ftrees[1]));
    // REQUIRE(std::ranges::equal(ftrees, std::views::join(new_ftrees)));
  }

  SECTION("test split middle") {
    // insert many items to ftrees[5]
    constexpr auto kItemsCount = 500ul;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[5].insert({i, static_cast<nibble>(i % 16)}));
    }
    REQUIRE(ftrees.ftrees()[4].insert({700, nibble{0}}));
    REQUIRE(ftrees.ftrees()[6].insert({800, nibble{0}}));
    REQUIRE(ftrees.ftrees()[2].insert({1000, nibble{0}}));

    key_type split_point;
    std::array<FTrees, 2> new_ftrees{FTrees{TestBlock::LoadMetadataBlock(test_device, 1)},
                                     FTrees{TestBlock::LoadMetadataBlock(test_device, 2)}};
    new_ftrees[0].Init();
    new_ftrees[1].Init();
    ftrees.split(new_ftrees[0], new_ftrees[1], split_point);

    // Should be the middle key of the ftree with most items
    CHECK(split_point == 250);
  }
}
