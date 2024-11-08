/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <random>
#include <ranges>

#include <wfslib/wfs_device.h>

#include "../src/directory_map.h"
#include "../src/free_blocks_allocator.h"
#include "../src/quota_area.h"

#include "utils/test_blocks_device.h"
#include "utils/test_utils.h"

namespace {

class TestAttributes {
 public:
  TestAttributes(uint8_t log2_size = 6) : data_{static_cast<uint16_t>(1u << log2_size), std::byte{0}} {
    assert(log2_size >= 6 && log2_size <= 10);
    data()->entry_log2_size = log2_size;
  }

  Attributes* data() { return reinterpret_cast<Attributes*>(data_.data()); }

 private:
  std::vector<std::byte> data_;
};

};  // namespace

TEST_CASE("DirectoryMapTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto wfs_device = *WfsDevice::Create(test_device);
  auto root_block = *wfs_device->GetRootArea()->LoadMetadataBlock(10);
  DirectoryMap dir_tree{wfs_device->GetRootArea(), root_block};
  dir_tree.Init();

  SECTION("Check empty tree size") {
    REQUIRE(dir_tree.size() == 0);
  }

  SECTION("insert items sorted") {
    const int kItemsCount = 100000;
    TestAttributes attributes(6);
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      attributes.data()->flags = i;
      REQUIRE(dir_tree.insert(std::format("{:05}", i), attributes.data()));
    }
    REQUIRE(dir_tree.size() == kItemsCount);
    REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == kItemsCount);
    for (auto [i, item] : std::views::enumerate(dir_tree)) {
      CHECK(item.name == std::format("{:05}", i));
      CHECK(item.attributes.get()->flags.value() == i);
      CHECK(item.attributes.get()->entry_log2_size.value() == 6);
    }
    REQUIRE(std::ranges::equal(
        std::views::transform(std::views::reverse(dir_tree),
                              [](const auto& item) -> int { return item.attributes.get()->flags.value(); }),
        std::views::reverse(std::views::iota(0, kItemsCount))));
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      auto item = dir_tree.find(std::format("{:05}", i));
      REQUIRE(!item.is_end());
      CHECK((*item).attributes.get()->flags.value() == i);
      CHECK((*item).name == std::format("{:05}", i));
    }
  }

  SECTION("insert and remove one two items") {
    SubBlockAllocator<DirectoryTreeHeader> allocator{root_block};
    auto free_bytes_1 = allocator.GetFreeBytes();
    TestAttributes attributes(6);
    REQUIRE(dir_tree.insert("a", attributes.data()));
    auto free_bytes_2 = allocator.GetFreeBytes();
    CHECK(free_bytes_2 != free_bytes_1);
    REQUIRE(dir_tree.insert("aa", attributes.data()));
    CHECK(free_bytes_2 != allocator.GetFreeBytes());
    REQUIRE(dir_tree.erase("aa"));
    CHECK(free_bytes_2 == allocator.GetFreeBytes());
    REQUIRE(dir_tree.erase("a"));
    CHECK(free_bytes_1 == allocator.GetFreeBytes());
  }

  SECTION("insert items unsorted") {
    constexpr int kItemsCount = 100000;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    TestAttributes attributes(6);
    for (auto i : unsorted_keys) {
      attributes.data()->flags = i;
      REQUIRE(dir_tree.insert(std::format("{:05}", i), attributes.data()));
    }
    REQUIRE(dir_tree.size() == kItemsCount);
    REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == kItemsCount);
    for (auto [i, item] : std::views::enumerate(dir_tree)) {
      CHECK(item.name == std::format("{:05}", i));
      CHECK(item.attributes.get()->flags.value() == i);
      CHECK(item.attributes.get()->entry_log2_size.value() == 6);
    }
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      auto item = dir_tree.find(std::format("{:05}", i));
      REQUIRE(!item.is_end());
      CHECK((*item).attributes.get()->flags.value() == i);
      CHECK((*item).name == std::format("{:05}", i));
    }
  }

  SECTION("insert items unsorted different attributes size") {
    constexpr int kItemsCount = 10000;
    auto unsorted_keys = createShuffledKeysArray<kItemsCount>();
    for (auto i : unsorted_keys) {
      TestAttributes attributes((i % 5) + 6);
      attributes.data()->flags = i;
      REQUIRE(dir_tree.insert(std::format("{:04}", i), attributes.data()));
    }
    REQUIRE(dir_tree.size() == kItemsCount);
    REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == kItemsCount);
    for (auto [i, item] : std::views::enumerate(dir_tree)) {
      CHECK(item.name == std::format("{:04}", i));
      CHECK(item.attributes.get()->flags.value() == i);
      CHECK(item.attributes.get()->entry_log2_size.value() == (i % 5) + 6);
    }
  }

  SECTION("remove items randomly") {
    constexpr int kItemsCount = 100000;
    auto free_blocks = (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count();
    TestAttributes attributes(6);
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      attributes.data()->flags = i;
      REQUIRE(dir_tree.insert(std::format("{:05}", i), attributes.data()));
    }
    CHECK(free_blocks != (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());

    auto unsorted_indexes = createShuffledKeysArray<kItemsCount>();
    auto middle = unsorted_indexes.begin() + kItemsCount / 2;
    // Remove half the items first
    for (auto i : std::ranges::subrange(unsorted_indexes.begin(), middle)) {
      REQUIRE(dir_tree.erase(std::format("{:05}", i)));
    }

    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_indexes.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    // Ensure that the right items were deleted
    REQUIRE(std::ranges::equal(
        std::views::transform(dir_tree, [](const auto& item) -> int { return item.attributes.get()->flags.value(); }),
        sorted_upper_half));

    // Remove the second half
    for (auto i : std::ranges::subrange(middle, unsorted_indexes.end())) {
      REQUIRE(dir_tree.erase(std::format("{:05}", i)));
    }

    // Should be empty
    CHECK(dir_tree.size() == 0);
    CHECK(dir_tree.begin() == dir_tree.end());
    // Everything should have been released.
    CHECK(free_blocks == (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());
  }

  SECTION("remove items randomly") {
    constexpr int kItemsCount = 100000;
    auto free_blocks = (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count();
    TestAttributes attributes(6);
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      attributes.data()->flags = i;
      REQUIRE(dir_tree.insert(std::format("{:05}", i), attributes.data()));
    }
    CHECK(free_blocks != (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());

    auto unsorted_indexes = createShuffledKeysArray<kItemsCount>();
    auto middle = unsorted_indexes.begin() + kItemsCount / 2;
    // Remove half the items first
    for (auto i : std::ranges::subrange(unsorted_indexes.begin(), middle)) {
      REQUIRE(dir_tree.erase(std::format("{:05}", i)));
    }

    auto sorted_upper_half = std::ranges::subrange(middle, unsorted_indexes.end()) | std::ranges::to<std::vector>();
    std::ranges::sort(sorted_upper_half);
    // Ensure that the right items were deleted
    REQUIRE(std::ranges::equal(
        std::views::transform(dir_tree, [](const auto& item) -> int { return item.attributes.get()->flags.value(); }),
        sorted_upper_half));

    // Remove the second half
    for (auto i : std::ranges::subrange(middle, unsorted_indexes.end())) {
      REQUIRE(dir_tree.erase(std::format("{:05}", i)));
    }

    // Should be empty
    CHECK(dir_tree.size() == 0);
    CHECK(dir_tree.begin() == dir_tree.end());
    // Everything should have been released.
    CHECK(free_blocks == (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());
  }

  SECTION("check backward/forward iterator") {
    constexpr int kItemsCount = 100000;
    TestAttributes attributes(6);
    for (uint32_t i = 0; i < kItemsCount; ++i) {
      REQUIRE(dir_tree.insert(std::format("{:05}", i), attributes.data()));
    }

    auto it = dir_tree.begin();
    uint32_t steps = 0;
    while (it != dir_tree.end()) {
      REQUIRE((*it).name == std::format("{:05}", steps));
      ++it;
      ++steps;
    }
    REQUIRE(steps == kItemsCount);
    REQUIRE(it.is_end());
    while (it != dir_tree.begin()) {
      --it;
      --steps;
      REQUIRE((*it).name == std::format("{:05}", steps));
    }
    REQUIRE(steps == 0);
    REQUIRE(it.is_begin());

    for (int i = 0; i < 40; ++i) {
      ++it;
      ++steps;
      REQUIRE((*it).name == std::format("{:05}", steps));
    }
    for (int i = 0; i < 20; ++i) {
      --it;
      --steps;
      REQUIRE((*it).name == std::format("{:05}", steps));
    }
    REQUIRE((*it).name == std::format("{:05}", 20));
  }

  SECTION("split tree during erase") {
    TestAttributes attributes(6);
    for (uint32_t i = 0; i < 80; ++i) {
      REQUIRE(dir_tree.insert(std::format("{:05}", 10000 + i), attributes.data()));
    }
    for (uint32_t i = 0; i < 200; ++i) {
      REQUIRE(dir_tree.insert(std::format("{:05}", 11000 + i), attributes.data()));
    }
    SubBlockAllocator<DirectoryTreeHeader> allocator{*wfs_device->GetRootArea()->LoadMetadataBlock(10)};
    // Fill it so we won't be able to go to the merge flow
    while (allocator.Alloc(8))
      ;
    for (uint32_t i = 0; i < 200; ++i) {
      REQUIRE(dir_tree.erase(std::format("{:05}", 11000 + i)));
    }
    for (auto [i, item] : std::views::enumerate(dir_tree)) {
      CHECK(item.name == std::format("{:05}", 10000 + i));
    }
    for (uint32_t i = 0; i < 80; ++i) {
      auto item = dir_tree.find(std::format("{:05}", 10000 + i));
      REQUIRE(!item.is_end());
      CHECK((*item).name == std::format("{:05}", 10000 + i));
    }
  }
}
