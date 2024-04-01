/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "../src/ftree.h"

#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"
#include "test_metadata_block.h"
#include "test_utils.h"

TEST_CASE("FTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto ftrees_block = TestMetadataBlock::LoadBlock(test_device, 0);
  FTreesBlock{ftrees_block}.Init();

  auto ftrees = std::ranges::iota_view(size_t{0}, kSizeBucketsCount) |
                std::views::transform([&ftrees_block](size_t i) -> FTree {
                  return FTree{ftrees_block, i};
                }) |
                std::ranges::to<std::vector>();

  SECTION("Check empty ftree size") {
    for (int i = 0; i < kSizeBucketsCount; ++i)
      REQUIRE(ftrees[i].size() == 0);
  }

  SECTION("insert and erase sorted item") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees[0].insert({i, static_cast<nibble>(i % 16)}));
    }

    // Tree should fill now
    uint32_t inserted = 0;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      inserted += ftrees[1].insert({i, static_cast<nibble>(i % 16)}) ? 1 : 0;
    }
    REQUIRE(inserted < kItemsCount);

    // should be full
    REQUIRE_FALSE(ftrees[2].insert({0, nibble{0}}));

    REQUIRE(ftrees[0].size() == kItemsCount);
    REQUIRE(ftrees[1].size() == inserted);
    REQUIRE(ftrees[2].size() == 0);

    REQUIRE(std::ranges::equal(
        std::views::transform(ftrees[0],
                              [](const FTree::iterator::value_type& extent) -> std::pair<uint32_t, nibble> {
                                return {extent.key, extent.value};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::pair<uint32_t, nibble> {
          return {i, static_cast<nibble>(i % 16)};
        })));
    REQUIRE(std::ranges::equal(
        std::views::transform(ftrees[1],
                              [](const FTree::iterator::value_type& extent) -> std::pair<uint32_t, nibble> {
                                return {extent.key, extent.value};
                              }),
        std::views::transform(std::views::iota(uint32_t{0}, inserted), [](int i) -> std::pair<uint32_t, nibble> {
          return {i, static_cast<nibble>(i % 16)};
        })));

    ftrees[0].erase(ftrees[0].begin(), ftrees[0].end());
    REQUIRE(ftrees[0].size() == 0);

    // Should be able to fill ftrees[2] now
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees[2].insert({i, static_cast<nibble>(i % 16)}));
    }
    REQUIRE(ftrees[2].size() == kItemsCount);
  }

  SECTION("insert compact") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees[0].insert({i, static_cast<nibble>(i % 16)}));
    }
    REQUIRE(ftrees[0].size() == kItemsCount);

    FTree ftree{TestMetadataBlock::LoadBlock(test_device, 1), 0};
    ftree.Init();
    REQUIRE(ftree.insert_compact(ftrees[0].begin(), ftrees[0].end()));

    // Check that the tree is completly identical and valid.
    REQUIRE(ftree.size() == kItemsCount);
    REQUIRE(std::distance(ftree.begin(), ftree.end()) == kItemsCount);
    REQUIRE(std::ranges::equal(ftrees[0], ftree));
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftree.find(i)->key == i);
      REQUIRE(ftree.find(i)->value == static_cast<nibble>(i % 16));
    }
  }
}
