/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <ios>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

#include <wfslib/directory.h>
#include <wfslib/file.h>
#include <wfslib/wfs_device.h>

#include "directory_map.h"
#include "errors.h"
#include "file_layout.h"
#include "quota_area.h"
#include "structs.h"
#include "sub_block_allocator.h"

#include "utils/test_fixtures.h"

namespace {
constexpr std::string_view kTestFilename = "file";

class FileResizeFixture : public MetadataBlockFixture {
 public:
  FileResizeFixture() { directory_map.Init(); }

  std::shared_ptr<WfsDevice> wfs_device = *WfsDevice::Create(test_device);
  std::shared_ptr<QuotaArea> quota = wfs_device->GetRootArea();
  std::shared_ptr<Block> root_block = *quota->AllocMetadataBlock();
  DirectoryMap directory_map{quota, root_block};
  std::shared_ptr<Directory> directory = std::make_shared<Directory>("", Entry::MetadataRef{}, quota, root_block);

  std::shared_ptr<File> InsertInlineFile(std::string_view name, std::span<const std::byte> payload) {
    auto layout = FileLayout::Calculate(static_cast<uint32_t>(payload.size()), static_cast<uint8_t>(name.size()),
                                        quota->block_size_log2(), FileLayoutMode::MinimumForGrow);
    REQUIRE(layout.category == FileLayoutCategory::Inline);

    std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
    auto* metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->file_size = layout.file_size;
    metadata->size_on_disk = layout.size_on_disk;
    metadata->metadata_log2_size = layout.metadata_log2_size;
    metadata->size_category = FileLayout::CategoryValue(layout.category);
    metadata->filename_length = static_cast<uint8_t>(name.size());
    std::ranges::copy(payload, reinterpret_cast<std::byte*>(metadata) + metadata->size());

    REQUIRE(directory_map.insert(name, metadata));
    auto file = directory->GetFile(name);
    REQUIRE(file.has_value());
    return *file;
  }

  void InsertEmptyInlineFileMetadata(std::string_view name) {
    auto layout = FileLayout::Calculate(0, static_cast<uint8_t>(name.size()), quota->block_size_log2(),
                                        FileLayoutMode::MinimumForGrow);
    std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
    auto* metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->metadata_log2_size = layout.metadata_log2_size;
    metadata->size_category = FileLayout::CategoryValue(layout.category);
    metadata->filename_length = static_cast<uint8_t>(name.size());

    REQUIRE(directory_map.insert(name, metadata));
  }

  Entry::MetadataRef FindMetadata(std::string_view name) {
    auto it = directory->find(name);
    REQUIRE(!it.is_end());
    return (*it.base()).metadata;
  }
};

std::string EntryName(uint32_t index) {
  return std::format("{:05}", index);
}

std::vector<std::byte> Bytes(std::initializer_list<uint8_t> values) {
  return values | std::views::transform([](uint8_t value) { return std::byte{value}; }) |
         std::ranges::to<std::vector>();
}

std::vector<std::byte> ReadFile(const std::shared_ptr<File>& file, size_t size) {
  std::vector<std::byte> output(size);
  if (output.empty())
    return output;

  File::file_device device(file);
  const auto read = device.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
  REQUIRE(read == static_cast<std::streamsize>(output.size()));
  return output;
}

void WriteFile(const std::shared_ptr<File>& file, std::span<const std::byte> input, size_t offset) {
  File::file_device device(file);
  if (offset != 0)
    device.seek(static_cast<boost::iostreams::stream_offset>(offset), std::ios_base::beg);
  const auto wrote =
      device.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
  REQUIRE(wrote == static_cast<std::streamsize>(input.size()));
}

void RequireResizeError(const std::shared_ptr<File>& file, size_t new_size, WfsError error) {
  try {
    file->Resize(new_size);
    FAIL("Expected File::Resize to throw");
  } catch (const WfsException& e) {
    CHECK(e.error() == error);
  }
}
}  // namespace

TEST_CASE_METHOD(FileResizeFixture, "File resize grows inline data and zero fills the new range", "[file][resize]") {
  const auto initial = Bytes({0x11, 0x22, 0x33, 0x44});
  auto file = InsertInlineFile(kTestFilename, initial);

  file->Resize(12);

  CHECK(file->Size() == 12);
  CHECK(file->SizeOnDisk() == 12);
  auto expected = initial;
  expected.resize(12, std::byte{0});
  CHECK(ReadFile(file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks inline data", "[file][resize]") {
  const auto initial = Bytes({0x10, 0x20, 0x30, 0x40, 0x50});
  auto file = InsertInlineFile(kTestFilename, initial);

  file->Resize(3);

  CHECK(file->Size() == 3);
  CHECK(file->SizeOnDisk() == 3);
  CHECK(ReadFile(file, 3) == Bytes({0x10, 0x20, 0x30}));
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize reallocates inline metadata and supports writing after growth",
                 "[file][resize]") {
  const auto small_inline_capacity =
      (size_t{1} << 6) - FileLayout::BaseMetadataSize(static_cast<uint8_t>(kTestFilename.size()));
  std::vector<std::byte> initial(small_inline_capacity, std::byte{0x5a});
  auto file = InsertInlineFile(kTestFilename, initial);
  auto original_metadata = FindMetadata(kTestFilename);

  file->Resize(small_inline_capacity + 1);

  auto updated_metadata = FindMetadata(kTestFilename);
  CHECK(updated_metadata->metadata_log2_size.value() == 7);
  CHECK(updated_metadata != original_metadata);
  CHECK(file->Size() == small_inline_capacity + 1);
  CHECK(file->SizeOnDisk() == small_inline_capacity + 1);

  auto expected = initial;
  expected.push_back(std::byte{0});
  CHECK(ReadFile(file, expected.size()) == expected);

  constexpr std::array replacement{std::byte{0xbe}};
  WriteFile(file, replacement, small_inline_capacity);
  expected.back() = replacement.front();
  CHECK(ReadFile(file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize keeps directory lookup valid when inline metadata reallocation splits a leaf",
                 "[file][resize]") {
  constexpr uint32_t kEntriesCount = 80;
  constexpr uint32_t kTargetIndex = 40;
  const auto target = EntryName(kTargetIndex);
  const auto small_inline_capacity =
      (size_t{1} << 6) - FileLayout::BaseMetadataSize(static_cast<uint8_t>(target.size()));
  std::vector<std::byte> initial(small_inline_capacity, std::byte{0x4d});

  for (uint32_t i = 0; i < kEntriesCount; ++i) {
    if (i == kTargetIndex) {
      InsertInlineFile(target, initial);
    } else {
      InsertEmptyInlineFileMetadata(EntryName(i));
    }
  }

  SubBlockAllocator<DirectoryTreeHeader> allocator{root_block};
  while (allocator.Alloc(8)) {
  }

  auto file = directory->GetFile(target);
  REQUIRE(file.has_value());
  (*file)->Resize(small_inline_capacity + 1);

  auto loaded_again = directory->GetFile(target);
  REQUIRE(loaded_again.has_value());
  CHECK((*loaded_again)->Size() == small_inline_capacity + 1);

  auto expected = initial;
  expected.push_back(std::byte{0});
  CHECK(ReadFile(*loaded_again, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize truncates inline data to zero", "[file][resize]") {
  const auto small_inline_capacity =
      (size_t{1} << 6) - FileLayout::BaseMetadataSize(static_cast<uint8_t>(kTestFilename.size()));
  std::vector<std::byte> initial(small_inline_capacity + 1, std::byte{0x6b});
  auto file = InsertInlineFile(kTestFilename, initial);

  file->Resize(0);

  auto metadata = FindMetadata(kTestFilename);
  CHECK(metadata->metadata_log2_size.value() == 6);
  CHECK(file->Size() == 0);
  CHECK(file->SizeOnDisk() == 0);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize rejects category transitions for now", "[file][resize]") {
  const auto inline_capacity = FileLayout::InlineCapacity(static_cast<uint8_t>(kTestFilename.size()));
  std::vector<std::byte> initial(inline_capacity, std::byte{0x7c});
  auto file = InsertInlineFile(kTestFilename, initial);

  RequireResizeError(file, inline_capacity + 1, WfsError::kUnsupportedFileResize);

  CHECK(file->Size() == inline_capacity);
  CHECK(file->SizeOnDisk() == inline_capacity);
  CHECK(ReadFile(file, initial.size()) == initial);
}
