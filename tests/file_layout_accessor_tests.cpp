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
#include <string>
#include <string_view>
#include <vector>

#include <wfslib/device.h>
#include <wfslib/file.h>
#include <wfslib/wfs_device.h>

#include "block.h"
#include "file_layout.h"
#include "quota_area.h"
#include "structs.h"
#include "utils.h"

#include "utils/test_fixtures.h"

namespace {
constexpr std::string_view kTestFilename = "file";

struct TestFile {
  std::shared_ptr<File> file;
  std::shared_ptr<Block> metadata_block;
  EntryMetadata* metadata;
};

class FileLayoutAccessorFixture : public MetadataBlockFixture {
 public:
  std::shared_ptr<WfsDevice> wfs_device = *WfsDevice::Create(test_device);
  std::shared_ptr<QuotaArea> quota = wfs_device->GetRootArea();

  TestFile CreateFile(std::string_view name, const FileLayout& layout) {
    auto metadata_block = *quota->AllocMetadataBlock();
    auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
    std::fill(reinterpret_cast<std::byte*>(metadata),
              reinterpret_cast<std::byte*>(metadata) + (size_t{1} << layout.metadata_log2_size), std::byte{0});
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->file_size = layout.file_size;
    metadata->size_on_disk = layout.size_on_disk;
    metadata->metadata_log2_size = layout.metadata_log2_size;
    metadata->size_category = FileLayout::CategoryValue(layout.category);
    metadata->filename_length = static_cast<uint8_t>(name.size());

    auto file = std::make_shared<File>(std::string{name}, Entry::MetadataRef{metadata_block, 0}, quota);
    return {std::move(file), std::move(metadata_block), metadata};
  }

  TestFile CreateFile(std::string_view name, uint32_t file_size) {
    return CreateFile(name, FileLayout::Calculate(file_size, static_cast<uint8_t>(name.size()),
                                                  quota->block_size_log2(), FileLayoutMode::MinimumForGrow));
  }

  void StoreDataBlock(uint32_t area_block_number, std::span<const std::byte> data) {
    std::vector<std::byte> aligned_data(
        div_ceil(data.size(), test_device->device()->SectorSize()) * test_device->device()->SectorSize(), std::byte{0});
    std::ranges::copy(data, aligned_data.begin());
    test_device->blocks_[quota->to_physical_block_number(area_block_number)] = std::move(aligned_data);
  }
};

size_t ReadFile(const std::shared_ptr<File>& file, std::span<std::byte> output, size_t offset = 0) {
  File::file_device device(file);
  if (offset != 0)
    device.seek(static_cast<boost::iostreams::stream_offset>(offset), std::ios_base::beg);
  const auto read = device.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
  REQUIRE(read >= 0);
  return static_cast<size_t>(read);
}

size_t WriteFile(const std::shared_ptr<File>& file, std::span<const std::byte> input, size_t offset = 0) {
  File::file_device device(file);
  if (offset != 0)
    device.seek(static_cast<boost::iostreams::stream_offset>(offset), std::ios_base::beg);
  const auto wrote =
      device.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
  REQUIRE(wrote >= 0);
  return static_cast<size_t>(wrote);
}

std::span<std::byte> InlinePayload(EntryMetadata* metadata) {
  return {reinterpret_cast<std::byte*>(metadata) + metadata->size(), metadata->size_on_disk.value()};
}

template <typename T>
std::span<T> AlignedMetadataItems(EntryMetadata* metadata, size_t count) {
  auto* metadata_bytes = reinterpret_cast<std::byte*>(metadata);
  const auto metadata_size = sizeof(T) * count;
  auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
  return {reinterpret_cast<T*>(end - metadata_size), count};
}

void SetReversedBlockList(EntryMetadata* metadata, uint8_t block_size_log2, std::span<const uint32_t> block_numbers) {
  const auto category = FileLayout::CategoryFromValue(metadata->size_category.value());
  const auto metadata_items_count =
      FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), block_size_log2);
  REQUIRE(metadata_items_count == block_numbers.size());

  auto block_refs = AlignedMetadataItems<DataBlockMetadata>(metadata, metadata_items_count);
  for (size_t i = 0; i < block_numbers.size(); ++i) {
    block_refs[block_numbers.size() - i - 1].block_number = block_numbers[i];
  }
}
}  // namespace

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes inline data",
                 "[file-layout-accessor][unit]") {
  constexpr std::array initial_data{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
  constexpr std::array replacement_data{std::byte{0xaa}, std::byte{0xbb}};
  constexpr std::array expected_data{std::byte{0x11}, std::byte{0xaa}, std::byte{0xbb}, std::byte{0x44}};

  auto source = CreateFile(kTestFilename, initial_data.size());
  REQUIRE(source.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  std::ranges::copy(initial_data, InlinePayload(source.metadata).begin());

  std::array<std::byte, initial_data.size()> output{};
  CHECK(ReadFile(source.file, output) == output.size());
  CHECK(std::ranges::equal(output, initial_data));

  CHECK(WriteFile(source.file, replacement_data, 1) == replacement_data.size());
  CHECK(ReadFile(source.file, output) == output.size());
  CHECK(std::ranges::equal(output, expected_data));
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads reversed block list metadata in logical order",
                 "[file-layout-accessor][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  constexpr std::array<uint32_t, 2> data_blocks{50, 51};
  constexpr std::array first_block_start{std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3}};
  constexpr std::array second_block_data{std::byte{0xb1}, std::byte{0xb2}, std::byte{0xb3}};
  auto test_file = CreateFile(kTestFilename, block_size + 3);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  SetReversedBlockList(test_file.metadata, quota->block_size_log2(), data_blocks);

  std::vector<std::byte> first_block_data(block_size, std::byte{0x99});
  std::ranges::copy(first_block_start, first_block_data.begin());
  StoreDataBlock(data_blocks[0], first_block_data);
  StoreDataBlock(data_blocks[1], second_block_data);

  std::array<std::byte, first_block_start.size()> first_output{};
  CHECK(ReadFile(test_file.file, first_output) == first_output.size());
  CHECK(std::ranges::equal(first_output, first_block_start));

  std::array<std::byte, second_block_data.size()> second_output{};
  CHECK(ReadFile(test_file.file, second_output, block_size) == second_output.size());
  CHECK(std::ranges::equal(second_output, second_block_data));
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads across external block boundaries",
                 "[file-layout-accessor][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  constexpr std::array<uint32_t, 2> data_blocks{70, 71};
  constexpr std::array second_block_data{std::byte{0xc1}, std::byte{0xc2}, std::byte{0xc3}};
  constexpr std::array expected_data{std::byte{0xd0}, std::byte{0xc1}, std::byte{0xc2}, std::byte{0xc3}};
  auto test_file = CreateFile(kTestFilename, block_size + second_block_data.size());
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  SetReversedBlockList(test_file.metadata, quota->block_size_log2(), data_blocks);

  std::vector<std::byte> first_block_data(block_size, std::byte{0x99});
  first_block_data.back() = expected_data[0];
  StoreDataBlock(data_blocks[0], first_block_data);
  StoreDataBlock(data_blocks[1], second_block_data);

  std::array<std::byte, expected_data.size()> output{};
  CHECK(ReadFile(test_file.file, output, block_size - 1) == output.size());
  CHECK(std::ranges::equal(output, expected_data));
}
