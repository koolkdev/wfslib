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
#include <wfslib/file.h>

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

void InsertFixedSizeFiles(DirectoryMap& dir_tree, uint32_t entries_count, int width = 5) {
  TestEntryMetadata metadata(6);
  metadata.data()->flags = EntryMetadata::UNENCRYPTED_FILE;
  metadata.data()->filename_length = static_cast<uint8_t>(width);
  for (uint32_t i = 0; i < entries_count; ++i) {
    metadata.data()->file_size = i;
    metadata.data()->size_on_disk = i;
    REQUIRE(dir_tree.insert(EntryName(i, width), metadata.data()));
  }
}

void RequireEntriesInOrder(const DirectoryMap& dir_tree, uint32_t entries_count, int width = 5) {
  REQUIRE(dir_tree.size() == entries_count);
  REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == static_cast<int>(entries_count));
  for (auto [index, entry] : std::views::enumerate(dir_tree)) {
    auto i = static_cast<uint32_t>(index);
    CHECK(entry.name == EntryName(i, width));
    CHECK(entry.metadata.get()->flags.value() == i);
  }
}

size_t MetadataSize(uint8_t log2_size) {
  return size_t{1} << log2_size;
}

void FillMetadataBytes(EntryMetadata* metadata, size_t start, size_t end, std::byte value) {
  auto* bytes = reinterpret_cast<std::byte*>(metadata);
  std::fill(bytes + start, bytes + end, value);
}

void FillMetadataPayload(EntryMetadata* metadata, uint8_t log2_size, std::byte value) {
  FillMetadataBytes(metadata, sizeof(EntryMetadata), MetadataSize(log2_size), value);
}

void FillMetadataPayload(Block::DataRef<EntryMetadata> metadata, uint8_t log2_size, std::byte value) {
  FillMetadataPayload(metadata.get_mutable(), log2_size, value);
}

bool MetadataPayloadEquals(Block::DataRef<EntryMetadata> metadata, size_t start, size_t end, std::byte value) {
  auto* bytes = reinterpret_cast<const std::byte*>(metadata.get());
  return std::all_of(bytes + start, bytes + end, [value](std::byte byte) { return byte == value; });
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
  for (auto [index, entry] : std::views::enumerate(dir_tree)) {
    auto i = static_cast<uint32_t>(index);
    CHECK(entry.name == EntryName(i, 4));
    CHECK(entry.metadata.get()->flags.value() == i);
    CHECK(entry.metadata.get()->metadata_log2_size.value() == (i % 5) + 6);
  }
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap metadata replacement reports missing entries",
                 "[directory-map][unit]") {
  TestEntryMetadata replacement(6);
  auto result = dir_tree.replace_metadata("missing", replacement.data());
  REQUIRE(!result.has_value());
  CHECK(result.error() == WfsError::kEntryNotFound);
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap metadata replacement keeps same-size allocations",
                 "[directory-map][unit]") {
  TestEntryMetadata metadata(6);
  metadata.data()->flags = 0x1234;
  REQUIRE(dir_tree.insert("a", metadata.data()));

  auto entry = dir_tree.find("a");
  REQUIRE(!entry.is_end());
  auto original_metadata = (*entry).metadata;

  TestEntryMetadata replacement(6);
  replacement.data()->flags = 0x5678;
  auto result = dir_tree.replace_metadata("a", replacement.data());

  REQUIRE(result.has_value());
  CHECK(result->block == original_metadata.block);
  CHECK(result->offset == original_metadata.offset);
  CHECK(result->get()->flags.value() == 0x5678);
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap metadata replacement grows metadata using prepared bytes",
                 "[directory-map][unit]") {
  TestEntryMetadata metadata(6);
  metadata.data()->flags = 0x2345;
  REQUIRE(dir_tree.insert("a", metadata.data()));

  auto original_entry = dir_tree.find("a");
  REQUIRE(!original_entry.is_end());
  FillMetadataPayload((*original_entry).metadata, 6, std::byte{0x5a});

  TestEntryMetadata replacement(8);
  replacement.data()->flags = 0x6789;
  FillMetadataBytes(replacement.data(), MetadataSize(8) - 16, MetadataSize(8), std::byte{0x6b});
  auto result = dir_tree.replace_metadata("a", replacement.data());

  REQUIRE(result.has_value());
  auto entry = dir_tree.find("a");
  REQUIRE(!entry.is_end());
  CHECK((*entry).metadata.block == result->block);
  CHECK((*entry).metadata.offset == result->offset);
  CHECK(result->get()->flags.value() == 0x6789);
  CHECK(result->get()->metadata_log2_size.value() == 8);
  CHECK(MetadataPayloadEquals(*result, sizeof(EntryMetadata), MetadataSize(6), std::byte{0}));
  CHECK(MetadataPayloadEquals(*result, MetadataSize(8) - 16, MetadataSize(8), std::byte{0x6b}));
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap metadata replacement shrinks metadata and frees space",
                 "[directory-map][unit]") {
  SubBlockAllocator<DirectoryTreeHeader> allocator{root_block};
  TestEntryMetadata metadata(8);
  metadata.data()->flags = 0x3456;
  REQUIRE(dir_tree.insert("a", metadata.data()));
  auto free_after_insert = allocator.GetFreeBytes();

  TestEntryMetadata replacement(6);
  replacement.data()->flags = 0x4567;
  auto result = dir_tree.replace_metadata("a", replacement.data());

  REQUIRE(result.has_value());
  CHECK(allocator.GetFreeBytes() > free_after_insert);
  CHECK(result->get()->flags.value() == 0x4567);
  CHECK(result->get()->metadata_log2_size.value() == 6);
  REQUIRE(dir_tree.erase("a"));
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap metadata replacement handles a forced leaf split",
                 "[directory-map][white-box]") {
  auto free_blocks = (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count();
  InsertFixedSizeEntries(dir_tree, 80);

  auto target = EntryName(40);

  SubBlockAllocator<DirectoryTreeHeader> allocator{root_block};
  while (allocator.Alloc(8)) {
  }

  TestEntryMetadata replacement(8);
  replacement.data()->flags = 40;
  FillMetadataBytes(replacement.data(), MetadataSize(8) - 16, MetadataSize(8), std::byte{0x6b});
  auto result = dir_tree.replace_metadata(target, replacement.data());

  REQUIRE(result.has_value());
  CHECK(result->get()->flags.value() == 40);
  CHECK(result->get()->metadata_log2_size.value() == 8);
  CHECK(MetadataPayloadEquals(*result, MetadataSize(8) - 16, MetadataSize(8), std::byte{0x6b}));
  RequireEntriesInOrder(dir_tree, 80);
  for (uint32_t i = 0; i < 80; ++i) {
    REQUIRE(dir_tree.erase(EntryName(i)));
  }
  CHECK(free_blocks == (*wfs_device->GetRootArea()->GetFreeBlocksAllocator())->free_blocks_count());
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap refreshes live metadata handles after a leaf split",
                 "[directory-map][white-box]") {
  auto block = *wfs_device->GetRootArea()->AllocMetadataBlock();
  auto map = std::make_shared<DirectoryMap>(wfs_device->GetRootArea(), block);
  map->Init();
  InsertFixedSizeFiles(*map, 80);

  constexpr uint32_t kLiveIndex = 10;
  auto live_it = map->find(EntryName(kLiveIndex));
  REQUIRE(!live_it.is_end());
  auto live_entry = map->LoadEntry(live_it);
  REQUIRE(live_entry.has_value());
  auto live_file = std::dynamic_pointer_cast<File>(*live_entry);
  REQUIRE(live_file);
  CHECK(live_file->Size() == kLiveIndex);

  SubBlockAllocator<DirectoryTreeHeader> allocator{block};
  while (allocator.Alloc(8)) {
  }

  TestEntryMetadata replacement(8);
  replacement.data()->flags = EntryMetadata::UNENCRYPTED_FILE;
  replacement.data()->filename_length = 5;
  replacement.data()->file_size = 40;
  replacement.data()->size_on_disk = 40;
  auto result = map->replace_metadata(EntryName(40), replacement.data());
  REQUIRE(result.has_value());

  auto current_live = map->find(EntryName(kLiveIndex));
  REQUIRE(!current_live.is_end());
  (*current_live).metadata.get_mutable()->file_size = 1234;
  CHECK(live_file->Size() == 1234);
}

TEST_CASE_METHOD(MetadataBlockFixture,
                 "WfsDevice returns the same live file instance for repeated path lookups",
                 "[directory-map][cache]") {
  auto wfs_device = *WfsDevice::Create(test_device);
  auto root_area = wfs_device->GetRootArea();
  auto root_block = *root_area->LoadMetadataBlock(3, /*new_block=*/true);
  DirectoryMap root_map{root_area, root_block};
  root_map.Init();

  TestEntryMetadata metadata(6);
  metadata.data()->flags = EntryMetadata::UNENCRYPTED_FILE;
  metadata.data()->filename_length = 4;
  metadata.data()->file_size = 12;
  metadata.data()->size_on_disk = 12;
  REQUIRE(root_map.insert("file", metadata.data()));

  auto first = wfs_device->GetFile("/file");
  REQUIRE(first);
  auto second = wfs_device->GetFile("/file");
  REQUIRE(second);
  CHECK(first == second);
}

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap metadata replacement works in a multi-level tree",
                 "[directory-map][integration]") {
  constexpr uint32_t kEntriesCount = 10000;
  InsertFixedSizeEntries(dir_tree, kEntriesCount);

  TestEntryMetadata replacement(8);
  replacement.data()->flags = 5678;
  auto result = dir_tree.replace_metadata(EntryName(5678), replacement.data());

  REQUIRE(result.has_value());
  CHECK(result->get()->flags.value() == 5678);
  CHECK(result->get()->metadata_log2_size.value() == 8);
  REQUIRE(dir_tree.size() == kEntriesCount);
  for (auto index : {uint32_t{0}, uint32_t{5678}, kEntriesCount - 1}) {
    auto entry = dir_tree.find(EntryName(index));
    REQUIRE(!entry.is_end());
    CHECK((*entry).metadata.get()->flags.value() == index);
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

TEST_CASE_METHOD(DirectoryMapFixture,
                 "DirectoryMap iterator walks forward and backward",
                 "[directory-map][iterator][stress]") {
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
  for (auto [index, entry] : std::views::enumerate(dir_tree)) {
    auto i = static_cast<uint32_t>(index);
    CHECK(entry.name == EntryName(10000 + i));
  }
  for (uint32_t i = 0; i < 80; ++i) {
    auto entry = dir_tree.find(EntryName(10000 + i));
    REQUIRE(!entry.is_end());
    CHECK((*entry).name == EntryName(10000 + i));
  }
}
