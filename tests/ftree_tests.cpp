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

#include "test_block.h"
#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"
#include "test_utils.h"

TEST_CASE("FTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto ftrees_block = TestBlock::LoadMetadataBlock(test_device, 0);
  FTreesBlock{ftrees_block}.Init();

  auto ftrees = std::ranges::iota_view(size_t{0}, kSizeBuckets.size()) |
                std::views::transform([&ftrees_block](size_t i) -> FTree {
                  return FTree{ftrees_block, i};
                }) |
                std::ranges::to<std::vector>();

  SECTION("Check empty ftree size") {
    for (size_t i = 0; i < kSizeBuckets.size(); ++i)
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

    FTree ftree{TestBlock::LoadMetadataBlock(test_device, 1), 0};
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

  SECTION("check backward/forward iterator") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees[0].insert({i, static_cast<nibble>(i % 16)}));
    }

    auto it = ftrees[0].begin();
    uint32_t steps = 0;
    while (it != ftrees[0].end()) {
      REQUIRE(it->key == steps);
      ++it;
      ++steps;
    }
    REQUIRE(steps == kItemsCount);
    REQUIRE(it.is_end());
    while (it != ftrees[0].begin()) {
      --it;
      --steps;
      REQUIRE(it->key == steps);
    }
    REQUIRE(steps == 0);
    REQUIRE(it.is_begin());

    for (int i = 0; i < 4; ++i) {
      ++it;
      ++steps;
      REQUIRE(it->key == steps);
    }
    for (int i = 0; i < 2; ++i) {
      --it;
      --steps;
      REQUIRE(it->key == steps);
    }
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
    REQUIRE(it->key == 22);
  }
}
