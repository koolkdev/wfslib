/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include <wfslib/wfs_device.h>

#include "directory_map.h"
#include "free_blocks_allocator.h"
#include "quota_area.h"
#include "sub_block_allocator.h"

#include "utils/test_fixtures.h"
#include "utils/test_utils.h"

namespace {

class TestEntryMetadata {
 public:
  TestEntryMetadata(uint8_t log2_size = 6) : data_{static_cast<uint16_t>(1u << log2_size), std::byte{0}} {
    assert(log2_size >= 6 && log2_size <= 10);
    data()->metadata_log2_size = log2_size;
  }

  EntryMetadata* data() { return reinterpret_cast<EntryMetadata*>(data_.data()); }

 private:
  std::vector<std::byte> data_;
};

class DirectoryMapFixture : public MetadataBlockFixture {
 public:
  DirectoryMapFixture() { dir_tree.Init(); }

  std::shared_ptr<WfsDevice> wfs_device = *WfsDevice::Create(test_device);
  std::shared_ptr<Block> root_block = *wfs_device->GetRootArea()->AllocMetadataBlock();
  DirectoryMap dir_tree{wfs_device->GetRootArea(), root_block};
};

std::string EntryName(uint32_t index, int width = 5) {
  return std::format("{:0{}}", index, width);
}

void InsertFixedSizeEntries(DirectoryMap& dir_tree, uint32_t entries_count, int width = 5) {
  TestEntryMetadata metadata(6);
  for (uint32_t i = 0; i < entries_count; ++i) {
    metadata.data()->flags = i;
    REQUIRE(dir_tree.insert(EntryName(i, width), metadata.data()));
  }
}

void RequireEntriesInOrder(const DirectoryMap& dir_tree, uint32_t entries_count, int width = 5) {
  REQUIRE(dir_tree.size() == entries_count);
  REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == static_cast<int>(entries_count));
  for (auto [i, entry] : std::views::enumerate(dir_tree)) {
    CHECK(entry.name == EntryName(i, width));
    CHECK(entry.metadata.get()->flags.value() == i);
  }
}

constexpr int kDirectoryMapStressEntries = 100000;

}  // namespace

TEST_CASE_METHOD(DirectoryMapFixture, "DirectoryMap is empty after initialization", "[directory-map][unit]") {
  REQUIRE(dir_tree.size() == 0);
}

TEST_CASE_METHOD(DirectoryMapFixture, "DirectoryMap inserts sorted entries", "[directory-map][stress]") {
  InsertFixedSizeEntries(dir_tree, kDirectoryMapStressEntries);

  RequireEntriesInOrder(dir_tree, kDirectoryMapStressEntries);
  REQUIRE(std::ranges::equal(
      std::views::transform(std::views::reverse(dir_tree),
                            [](const auto& entry) -> int { return entry.metadata.get()->flags.value(); }),
      std::views::reverse(std::views::iota(0, kDirectoryMapStressEntries))));

  for (uint32_t i = 0; i < kDirectoryMapStressEntries; ++i) {
    CAPTURE(i);
    auto entry = dir_tree.find(EntryName(i));
    REQUIRE(!entry.is_end());
    CHECK((*entry).metadata.get()->flags.value() == i);
    CHECK((*entry).name == EntryName(i));
  }
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap frees inline metadata after removing two entries",
                 "[directory-map][unit]") {
  SubBlockAllocator<DirectoryTreeHeader> allocator{root_block};
  auto free_bytes_1 = allocator.GetFreeBytes();
  TestEntryMetadata metadata(6);
  REQUIRE(dir_tree.insert("a", metadata.data()));
  auto free_bytes_2 = allocator.GetFreeBytes();
  CHECK(free_bytes_2 != free_bytes_1);
  REQUIRE(dir_tree.insert("aa", metadata.data()));
  CHECK(free_bytes_2 != allocator.GetFreeBytes());
  REQUIRE(dir_tree.erase("aa"));
  CHECK(free_bytes_2 == allocator.GetFreeBytes());
  REQUIRE(dir_tree.erase("a"));
  CHECK(free_bytes_1 == allocator.GetFreeBytes());
}

TEST_CASE_METHOD(DirectoryMapFixture, "DirectoryMap inserts shuffled entries", "[directory-map][stress]") {
  auto unsorted_keys = createShuffledKeysArray<kDirectoryMapStressEntries>();
  TestEntryMetadata metadata(6);
  for (auto i : unsorted_keys) {
    metadata.data()->flags = i;
    REQUIRE(dir_tree.insert(EntryName(i), metadata.data()));
  }

  RequireEntriesInOrder(dir_tree, kDirectoryMapStressEntries);
  for (uint32_t i = 0; i < kDirectoryMapStressEntries; ++i) {
    CAPTURE(i);
    auto entry = dir_tree.find(EntryName(i));
    REQUIRE(!entry.is_end());
    CHECK((*entry).metadata.get()->flags.value() == i);
    CHECK((*entry).name == EntryName(i));
  }
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap preserves variable metadata sizes",
                 "[directory-map][integration]") {
  constexpr int kEntriesCount = 10000;
  auto unsorted_keys = createShuffledKeysArray<kEntriesCount>();
  for (auto i : unsorted_keys) {
    TestEntryMetadata metadata((i % 5) + 6);
    metadata.data()->flags = i;
    REQUIRE(dir_tree.insert(EntryName(i, 4), metadata.data()));
  }

  REQUIRE(dir_tree.size() == kEntriesCount);
  REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == kEntriesCount);
  for (auto [i, entry] : std::views::enumerate(dir_tree)) {
    CHECK(entry.name == EntryName(i, 4));
    CHECK(entry.metadata.get()->flags.value() == i);
    CHECK(entry.metadata.get()->metadata_log2_size.value() == (i % 5) + 6);
  }
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap removes shuffled entries and releases blocks",
                 "[directory-map][stress]") {
  auto free_blocks = (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count();
  InsertFixedSizeEntries(dir_tree, kDirectoryMapStressEntries);
  CHECK(free_blocks != (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());

  auto unsorted_indexes = createShuffledKeysArray<kDirectoryMapStressEntries>();
  auto middle = unsorted_indexes.begin() + kDirectoryMapStressEntries / 2;
  for (auto i : std::ranges::subrange(unsorted_indexes.begin(), middle)) {
    CAPTURE(i);
    REQUIRE(dir_tree.erase(EntryName(i)));
  }

  auto sorted_upper_half = std::ranges::to<std::vector>(std::ranges::subrange(middle, unsorted_indexes.end()));
  std::ranges::sort(sorted_upper_half);
  REQUIRE(std::ranges::equal(
      std::views::transform(dir_tree, [](const auto& entry) -> int { return entry.metadata.get()->flags.value(); }),
      sorted_upper_half));

  for (auto i : std::ranges::subrange(middle, unsorted_indexes.end())) {
    CAPTURE(i);
    REQUIRE(dir_tree.erase(EntryName(i)));
  }

  CHECK(dir_tree.size() == 0);
  CHECK(dir_tree.begin() == dir_tree.end());
  CHECK(free_blocks == (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());
}

TEST_CASE_METHOD(DirectoryMapFixture, "DirectoryMap iterator walks forward and backward", "[directory-map][iterator][stress]") {
  TestEntryMetadata metadata(6);
  for (uint32_t i = 0; i < kDirectoryMapStressEntries; ++i) {
    REQUIRE(dir_tree.insert(EntryName(i), metadata.data()));
  }

  auto it = dir_tree.begin();
  uint32_t steps = 0;
  while (it != dir_tree.end()) {
    REQUIRE((*it).name == EntryName(steps));
    ++it;
    ++steps;
  }
  REQUIRE(steps == kDirectoryMapStressEntries);
  REQUIRE(it.is_end());
  while (it != dir_tree.begin()) {
    --it;
    --steps;
    REQUIRE((*it).name == EntryName(steps));
  }
  REQUIRE(steps == 0);
  REQUIRE(it.is_begin());

  for (int i = 0; i < 40; ++i) {
    ++it;
    ++steps;
    REQUIRE((*it).name == EntryName(steps));
  }
  for (int i = 0; i < 20; ++i) {
    --it;
    --steps;
    REQUIRE((*it).name == EntryName(steps));
  }
  REQUIRE((*it).name == EntryName(20));
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap keeps remaining entries when erase must split instead of merge",
                 "[directory-map][white-box]") {
  TestEntryMetadata metadata(6);
  for (uint32_t i = 0; i < 80; ++i) {
    REQUIRE(dir_tree.insert(EntryName(10000 + i), metadata.data()));
  }
  for (uint32_t i = 0; i < 200; ++i) {
    REQUIRE(dir_tree.insert(EntryName(11000 + i), metadata.data()));
  }

  SubBlockAllocator<DirectoryTreeHeader> allocator{*wfs_device->GetRootArea()->LoadMetadataBlock(10)};
  while (allocator.Alloc(8)) {
  }

  for (uint32_t i = 0; i < 200; ++i) {
    REQUIRE(dir_tree.erase(EntryName(11000 + i)));
  }
  for (auto [i, entry] : std::views::enumerate(dir_tree)) {
    CHECK(entry.name == EntryName(10000 + i));
  }
  for (uint32_t i = 0; i < 80; ++i) {
    auto entry = dir_tree.find(EntryName(10000 + i));
    REQUIRE(!entry.is_end());
    CHECK((*entry).name == EntryName(10000 + i));
  }
}
