/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "../src/ftrees.h"

#include "test_blocks_device.h"
#include "test_free_blocks_allocator.h"
#include "test_metadata_block.h"
#include "test_utils.h"

TEST_CASE("FTreesTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto ftrees_block = TestMetadataBlock::LoadBlock(test_device, 0);
  FTrees ftrees{ftrees_block};
  ftrees.Init();

  SECTION("Check empty ftrees empty") {
    REQUIRE(ftrees.empty());
  }

  SECTION("insert sorted items") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBucketsCount].insert({i, static_cast<nibble>(i % 16)}));
    }
    REQUIRE(!ftrees.empty());
    REQUIRE(ftrees.size() == kItemsCount);

    REQUIRE(std::ranges::equal(
        std::views::transform(ftrees,
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, kItemsCount), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
        })));
  }

  SECTION("check backward iterator") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBucketsCount].insert({i, static_cast<nibble>(i % 16)}));
    }

    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(ftrees.rbegin(), ftrees.rend()),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::reverse(std::views::iota(0, kItemsCount)),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));
  }

  SECTION("check find") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBucketsCount].insert({i * 2, static_cast<nibble>(i % 16)}));
    }

    auto it = ftrees.find(523);
    REQUIRE(it->key == 522);

    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(ftrees.begin(), it),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, 522 / 2), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
        })));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(it, ftrees.end()),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(522 / 2, kItemsCount),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));

    it = ftrees.find(840);
    REQUIRE(it->key == 840);

    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(ftrees.begin(), it),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(0, 840 / 2), [](int i) -> std::tuple<uint32_t, nibble, size_t> {
          return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
        })));
    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(it, ftrees.end()),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::iota(840 / 2, kItemsCount),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));
  }

  SECTION("check rfind") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBucketsCount].insert({i * 2, static_cast<nibble>(i % 16)}));
    }
    auto it = ftrees.rfind(241);
    REQUIRE(it->key == 240);

    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(ftrees.rbegin(), it),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::reverse(std::views::iota(242 / 2, kItemsCount)),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));
    REQUIRE(std::ranges::equal(
        std::views::transform(
            std::ranges::subrange(it, ftrees.rend()),
            [](const FTrees::reverse_iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
              return {extent.key, extent.value, extent.bucket_index};
            }),
        std::views::transform(std::views::reverse(std::views::iota(0, 242 / 2)),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));
    it = ftrees.rfind(790);
    REQUIRE(it->key == 790);

    REQUIRE(std::ranges::equal(
        std::views::transform(std::ranges::subrange(ftrees.rbegin(), it),
                              [](const FTrees::iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
                                return {extent.key, extent.value, extent.bucket_index};
                              }),
        std::views::transform(std::views::reverse(std::views::iota(792 / 2, kItemsCount)),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));
    REQUIRE(std::ranges::equal(
        std::views::transform(
            std::ranges::subrange(it, ftrees.rend()),
            [](const FTrees::reverse_iterator::value_type& extent) -> std::tuple<uint32_t, nibble, size_t> {
              return {extent.key, extent.value, extent.bucket_index};
            }),
        std::views::transform(std::views::reverse(std::views::iota(0, 792 / 2)),
                              [](int i) -> std::tuple<uint32_t, nibble, size_t> {
                                return {i * 2, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBucketsCount)};
                              })));
  }

  SECTION("test split") {
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[i % kSizeBucketsCount].insert({i, static_cast<nibble>(i % 16)}));
    }

    key_type split_point;
    std::array<FTrees, 2> new_ftrees{FTrees{TestMetadataBlock::LoadBlock(test_device, 1)},
                                     FTrees{TestMetadataBlock::LoadBlock(test_device, 2)}};
    new_ftrees[0].Init();
    new_ftrees[1].Init();
    ftrees.split(new_ftrees[0], new_ftrees[1], split_point);

    // Should be the middle key of the ftree with most items, so somewhere around the actual center in our case
    REQUIRE(split_point == 252);

    REQUIRE(new_ftrees[0].size() == split_point);
    REQUIRE(new_ftrees[1].size() == kItemsCount - split_point);

    REQUIRE(std::ranges::size(new_ftrees[0]) == split_point);
    REQUIRE(std::ranges::size(new_ftrees[1]) == kItemsCount - split_point);

    REQUIRE(std::ranges::distance(new_ftrees[0].begin(), new_ftrees[0].end()) == static_cast<int>(split_point));
    REQUIRE(std::ranges::distance(new_ftrees[1].begin(), new_ftrees[1].end()) ==
            static_cast<int>(kItemsCount - split_point));

    REQUIRE(std::ranges::equal(std::ranges::subrange(ftrees.begin(), ftrees.find(split_point)), new_ftrees[0]));
    REQUIRE(std::ranges::equal(std::ranges::subrange(ftrees.find(split_point), ftrees.end()), new_ftrees[1]));
    // REQUIRE(std::ranges::equal(ftrees, std::views::join(new_ftrees)));
  }

  SECTION("test split middle") {
    // insert many items to ftrees[5]
    constexpr int kItemsCount = 500;
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(ftrees.ftrees()[5].insert({i, static_cast<nibble>(i % 16)}));
    }
    REQUIRE(ftrees.ftrees()[4].insert({700, nibble{0}}));
    REQUIRE(ftrees.ftrees()[6].insert({800, nibble{0}}));
    REQUIRE(ftrees.ftrees()[2].insert({1000, nibble{0}}));

    key_type split_point;
    std::array<FTrees, 2> new_ftrees{FTrees{TestMetadataBlock::LoadBlock(test_device, 1)},
                                     FTrees{TestMetadataBlock::LoadBlock(test_device, 2)}};
    new_ftrees[0].Init();
    new_ftrees[1].Init();
    ftrees.split(new_ftrees[0], new_ftrees[1], split_point);

    // Should be the middle key of the ftree with most items
    REQUIRE(split_point == 250);
  }
}
