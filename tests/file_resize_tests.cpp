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
#include <ios>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <wfslib/device.h>
#include <wfslib/file.h>
#include <wfslib/wfs_device.h>

#include "block.h"
#include "directory_map.h"
#include "file_layout.h"
#include "quota_area.h"
#include "structs.h"
#include "utils/test_fixtures.h"

namespace {
constexpr std::string_view kTestFilename = "file";
constexpr std::array kInitialData{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
constexpr std::array kReplacementData{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};

struct TestFile {
  std::shared_ptr<File> file;
  EntryMetadata* metadata;
};

class FileResizeFixture : public MetadataBlockFixture {
 public:
  std::shared_ptr<WfsDevice> wfs_device = *WfsDevice::Create(test_device);
  std::shared_ptr<QuotaArea> quota = wfs_device->GetRootArea();
  std::shared_ptr<Block> directory_block = *quota->AllocMetadataBlock();
  std::shared_ptr<DirectoryMap> directory_map = std::make_shared<DirectoryMap>(quota, directory_block);

  FileResizeFixture() { directory_map->Init(); }

  TestFile CreateFile(std::string_view name, const FileLayout& layout) {
    std::vector<std::byte> metadata_storage(size_t{1} << layout.metadata_log2_size, std::byte{0});
    auto* metadata = reinterpret_cast<EntryMetadata*>(metadata_storage.data());
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->file_size = layout.file_size;
    metadata->size_on_disk = layout.size_on_disk;
    metadata->metadata_log2_size = layout.metadata_log2_size;
    metadata->size_category = FileLayout::CategoryValue(layout.category);
    metadata->filename_length = static_cast<uint8_t>(name.size());

    REQUIRE(directory_map->insert(name, metadata));
    auto it = directory_map->find(name);
    REQUIRE(!it.is_end());
    auto stored_metadata = (*it).metadata;
    auto entry = directory_map->LoadEntry(it);
    REQUIRE(entry.has_value());
    auto file = std::dynamic_pointer_cast<File>(*entry);
    REQUIRE(file);
    return {std::move(file), stored_metadata.get_mutable()};
  }

  TestFile CreateFile(std::string_view name, uint32_t file_size) {
    return CreateFile(name, FileLayout::Calculate(file_size, static_cast<uint8_t>(name.size()),
                                                  quota->block_size_log2(), FileLayoutMode::MinimumForGrow));
  }

  EntryMetadata* StoredMetadata(std::string_view name) {
    auto it = directory_map->find(name);
    REQUIRE(!it.is_end());
    return (*it).metadata.get_mutable();
  }
};

std::span<std::byte> InlinePayload(EntryMetadata* metadata) {
  return {reinterpret_cast<std::byte*>(metadata) + metadata->size(), metadata->size_on_disk.value()};
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

size_t WriteFile(const std::shared_ptr<File>& file, std::span<const std::byte> input, size_t offset) {
  File::file_device device(file);
  if (offset != 0)
    device.seek(static_cast<boost::iostreams::stream_offset>(offset), std::ios_base::beg);
  const auto wrote =
      device.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
  REQUIRE(wrote >= 0);
  return static_cast<size_t>(wrote);
}
}  // namespace

TEST_CASE_METHOD(FileResizeFixture, "File resize grows empty inline file", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, 0);

  test_file.file->Resize(4);

  auto* metadata = StoredMetadata(kTestFilename);
  CHECK(test_file.file->Size() == 4);
  CHECK(test_file.file->SizeOnDisk() == 4);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CHECK(ReadFile(test_file.file, 4) == std::vector<std::byte>(4, std::byte{0}));
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks inline file", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());

  test_file.file->Resize(2);

  auto* metadata = StoredMetadata(kTestFilename);
  const std::vector expected{kInitialData[0], kInitialData[1]};
  CHECK(test_file.file->Size() == expected.size());
  CHECK(test_file.file->SizeOnDisk() == expected.size());
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize grows inline file across metadata allocation sizes",
                 "[file-resize][unit]") {
  constexpr uint32_t kGrownSize = 80;
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());

  const auto old_metadata_log2_size = test_file.metadata->metadata_log2_size.value();
  const auto target_layout = FileLayout::Calculate(kGrownSize, static_cast<uint8_t>(kTestFilename.size()),
                                                   quota->block_size_log2(), FileLayoutMode::MinimumForGrow);
  REQUIRE(target_layout.category == FileLayoutCategory::Inline);
  REQUIRE(target_layout.metadata_log2_size > old_metadata_log2_size);

  test_file.file->Resize(kGrownSize);

  auto* metadata = StoredMetadata(kTestFilename);
  std::vector expected(kGrownSize, std::byte{0});
  std::ranges::copy(kInitialData, expected.begin());
  CHECK(test_file.file->Size() == kGrownSize);
  CHECK(test_file.file->SizeOnDisk() == kGrownSize);
  CHECK(metadata->metadata_log2_size.value() == target_layout.metadata_log2_size);
  CHECK(ReadFile(test_file.file, kGrownSize) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize allows writing after inline growth", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());

  test_file.file->Resize(kInitialData.size() + kReplacementData.size());

  CHECK(WriteFile(test_file.file, kReplacementData, kInitialData.size()) == kReplacementData.size());
  std::vector<std::byte> expected{kInitialData.begin(), kInitialData.end()};
  expected.insert(expected.end(), kReplacementData.begin(), kReplacementData.end());
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize truncates inline file to zero", "[file-resize][unit]") {
  constexpr uint32_t kInitialSize = 80;
  auto test_file = CreateFile(kTestFilename, kInitialSize);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));

  const auto target_layout = FileLayout::Calculate(0, static_cast<uint8_t>(kTestFilename.size()),
                                                   quota->block_size_log2(), FileLayoutMode::MaximumForShrink);

  test_file.file->Resize(0);

  auto* metadata = StoredMetadata(kTestFilename);
  CHECK(test_file.file->Size() == 0);
  CHECK(test_file.file->SizeOnDisk() == 0);
  CHECK(metadata->metadata_log2_size.value() == target_layout.metadata_log2_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CHECK(InlinePayload(metadata).empty());
}

TEST_CASE_METHOD(FileResizeFixture, "File resize throws for non-inline layouts", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));

  CHECK_THROWS_AS(test_file.file->Resize(FileLayout::InlineCapacity(static_cast<uint8_t>(kTestFilename.size())) + 1),
                  std::logic_error);
}
