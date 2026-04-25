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
#include <wfslib/directory.h>
#include <wfslib/file.h>
#include <wfslib/wfs_device.h>

#include "block.h"
#include "directory_map.h"
#include "file_layout.h"
#include "quota_area.h"
#include "structs.h"
#include "utils.h"

#include "utils/test_fixtures.h"

namespace {
constexpr std::string_view kTestFilename = "file";
constexpr std::array kInitialData{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
constexpr uint32_t kInitialDataSize = static_cast<uint32_t>(kInitialData.size());
constexpr std::array kReplacementData{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};

struct TestFile {
  std::shared_ptr<File> file;
  std::shared_ptr<Block> metadata_block;
  EntryMetadata* metadata;
};

class FileLayoutAccessorFixture : public MetadataBlockFixture {
 public:
  std::shared_ptr<WfsDevice> wfs_device = *WfsDevice::Create(test_device);
  std::shared_ptr<QuotaArea> quota = wfs_device->GetRootArea();
  std::shared_ptr<Block> root_block = *quota->AllocMetadataBlock();
  DirectoryMap directory_map{quota, root_block};
  std::shared_ptr<Directory> directory = std::make_shared<Directory>("", Entry::MetadataRef{}, quota, root_block);

  FileLayoutAccessorFixture() { directory_map.Init(); }

  TestFile CreateFile(std::string_view name, const FileLayout& layout) {
    std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
    auto* metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->file_size = layout.file_size;
    metadata->size_on_disk = layout.size_on_disk;
    metadata->metadata_log2_size = layout.metadata_log2_size;
    metadata->size_category = FileLayout::CategoryValue(layout.category);
    metadata->filename_length = static_cast<uint8_t>(name.size());

    REQUIRE(directory_map.insert(name, metadata));
    auto file = directory->GetFile(name);
    REQUIRE(file.has_value());
    auto it = directory->find(name);
    REQUIRE(!it.is_end());
    auto metadata_ref = (*it.base()).metadata;
    return {*file, metadata_ref.block, metadata_ref.get_mutable()};
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

void SetReversedClusterList(EntryMetadata* metadata,
                            uint8_t block_size_log2,
                            std::span<const uint32_t> cluster_block_numbers) {
  const auto category = FileLayout::CategoryFromValue(metadata->size_category.value());
  const auto metadata_items_count =
      FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), block_size_log2);
  REQUIRE(metadata_items_count == cluster_block_numbers.size());

  auto cluster_refs = AlignedMetadataItems<DataBlocksClusterMetadata>(metadata, metadata_items_count);
  for (size_t i = 0; i < cluster_block_numbers.size(); ++i) {
    cluster_refs[cluster_block_numbers.size() - i - 1].block_number = cluster_block_numbers[i];
  }
}

void SetReversedMetadataBlockList(EntryMetadata* metadata,
                                  uint8_t block_size_log2,
                                  std::span<const uint32_t> metadata_block_numbers) {
  const auto category = FileLayout::CategoryFromValue(metadata->size_category.value());
  const auto metadata_items_count =
      FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), block_size_log2);
  REQUIRE(metadata_items_count == metadata_block_numbers.size());

  auto metadata_block_refs = AlignedMetadataItems<uint32_be_t>(metadata, metadata_items_count);
  for (size_t i = 0; i < metadata_block_numbers.size(); ++i) {
    metadata_block_refs[metadata_block_numbers.size() - i - 1] = metadata_block_numbers[i];
  }
}

std::span<DataBlocksClusterMetadata> ClusterMetadataItems(const std::shared_ptr<Block>& metadata_block, size_t count) {
  return {metadata_block->get_mutable_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader)), count};
}

template <size_t Size>
void RequireReadThenReplace(const std::shared_ptr<File>& file,
                            size_t offset,
                            const std::array<std::byte, Size>& initial_data,
                            const std::array<std::byte, Size>& replacement_data) {
  std::array<std::byte, Size> output{};
  CHECK(ReadFile(file, output, offset) == output.size());
  CHECK(std::ranges::equal(output, initial_data));

  CHECK(WriteFile(file, replacement_data, offset) == replacement_data.size());

  std::ranges::fill(output, std::byte{0});
  CHECK(ReadFile(file, output, offset) == output.size());
  CHECK(std::ranges::equal(output, replacement_data));
}
}  // namespace

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes inline data",
                 "[file-layout-accessor][unit]") {
  auto source = CreateFile(kTestFilename, kInitialDataSize);
  REQUIRE(source.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  std::ranges::copy(kInitialData, InlinePayload(source.metadata).begin());

  RequireReadThenReplace(source.file, 0, kInitialData, kReplacementData);
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes reversed block list metadata in logical order",
                 "[file-layout-accessor][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  constexpr std::array<uint32_t, 2> data_blocks{50, 51};
  auto test_file = CreateFile(kTestFilename, block_size + kInitialDataSize);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  SetReversedBlockList(test_file.metadata, quota->block_size_log2(), data_blocks);

  std::vector<std::byte> first_block_data(block_size, std::byte{0x99});
  StoreDataBlock(data_blocks[0], first_block_data);
  StoreDataBlock(data_blocks[1], kInitialData);

  RequireReadThenReplace(test_file.file, block_size, kInitialData, kReplacementData);
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes across external block boundaries",
                 "[file-layout-accessor][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  constexpr std::array<uint32_t, 2> data_blocks{70, 71};
  auto test_file = CreateFile(kTestFilename, block_size + kInitialDataSize - 1);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  SetReversedBlockList(test_file.metadata, quota->block_size_log2(), data_blocks);

  std::vector<std::byte> first_block_data(block_size, std::byte{0x99});
  first_block_data.back() = kInitialData[0];
  StoreDataBlock(data_blocks[0], first_block_data);
  StoreDataBlock(data_blocks[1], std::span{kInitialData}.subspan(1));

  RequireReadThenReplace(test_file.file, block_size - 1, kInitialData, kReplacementData);
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes large block metadata",
                 "[file-layout-accessor][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  constexpr std::array<uint32_t, 1> data_blocks{120};
  const auto offset = 5 * block_size;
  auto test_file = CreateFile(kTestFilename, offset + kInitialDataSize);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  SetReversedBlockList(test_file.metadata, quota->block_size_log2(), data_blocks);

  std::vector<std::byte> large_block_data(offset + kInitialData.size(), std::byte{0x99});
  REQUIRE(large_block_data.size() <= large_block_size);
  std::ranges::copy(kInitialData, large_block_data.begin() + offset);
  StoreDataBlock(data_blocks[0], large_block_data);

  RequireReadThenReplace(test_file.file, offset, kInitialData, kReplacementData);
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes reversed cluster metadata",
                 "[file-layout-accessor][unit]") {
  const auto large_block_size = static_cast<uint32_t>(quota->block_size() << log2_size(BlockType::Large));
  const auto cluster_size = static_cast<uint32_t>(quota->block_size() << log2_size(BlockType::Cluster));
  constexpr std::array<uint32_t, 2> cluster_blocks{240, 360};
  auto test_file = CreateFile(kTestFilename, cluster_size + kInitialDataSize);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  SetReversedClusterList(test_file.metadata, quota->block_size_log2(), cluster_blocks);

  StoreDataBlock(cluster_blocks[1], kInitialData);

  RequireReadThenReplace(test_file.file, cluster_size, kInitialData, kReplacementData);

  std::vector<std::byte> large_block_data(large_block_size, std::byte{0x99});
  std::ranges::copy(kInitialData, large_block_data.begin());
  StoreDataBlock(cluster_blocks[0] + 5 * (1 << log2_size(BlockType::Large)), large_block_data);
  RequireReadThenReplace(test_file.file, 5 * large_block_size, kInitialData, kReplacementData);
}

TEST_CASE_METHOD(FileLayoutAccessorFixture,
                 "File layout accessor reads and writes cluster metadata block lists",
                 "[file-layout-accessor][unit]") {
  const auto cluster_size = static_cast<uint32_t>(quota->block_size() << log2_size(BlockType::Cluster));
  const auto clusters_per_metadata_block = FileLayout::ClustersPerClusterMetadataBlock(quota->block_size_log2());
  const uint32_t offset = clusters_per_metadata_block * cluster_size;
  constexpr uint32_t data_block = 700;

  auto test_file = CreateFile(kTestFilename, offset + kInitialDataSize);
  REQUIRE(test_file.metadata->size_category.value() ==
          FileLayout::CategoryValue(FileLayoutCategory::ClusterMetadataBlocks));

  auto first_metadata_block = *quota->AllocMetadataBlock();
  auto second_metadata_block = *quota->AllocMetadataBlock();
  const std::array metadata_blocks{quota->to_area_block_number(first_metadata_block->physical_block_number()),
                                   quota->to_area_block_number(second_metadata_block->physical_block_number())};
  SetReversedMetadataBlockList(test_file.metadata, quota->block_size_log2(), metadata_blocks);

  ClusterMetadataItems(second_metadata_block, clusters_per_metadata_block)[0].block_number = data_block;
  StoreDataBlock(data_block, kInitialData);

  RequireReadThenReplace(test_file.file, offset, kInitialData, kReplacementData);
}
