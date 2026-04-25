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
#include <initializer_list>
#include <ios>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <wfslib/device.h>
#include <wfslib/directory.h>
#include <wfslib/file.h>
#include <wfslib/wfs_device.h>

#include "directory_map.h"
#include "errors.h"
#include "file_layout.h"
#include "free_blocks_allocator.h"
#include "quota_area.h"
#include "structs.h"
#include "sub_block_allocator.h"
#include "utils.h"

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

  std::shared_ptr<File> InsertExternalFile(FileLayoutCategory category,
                                           std::string_view name,
                                           std::span<const std::byte> payload) {
    auto layout = FileLayout::CalculateForCategory(
        static_cast<uint32_t>(payload.size()), static_cast<uint8_t>(name.size()), quota->block_size_log2(), category);
    REQUIRE(layout.category == category);

    std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
    auto* metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->file_size = layout.file_size;
    metadata->size_on_disk = layout.size_on_disk;
    metadata->metadata_log2_size = layout.metadata_log2_size;
    metadata->size_category = FileLayout::CategoryValue(layout.category);
    metadata->filename_length = static_cast<uint8_t>(name.size());

    auto allocation_type = AllocationBlockType(category);
    auto block_numbers = *quota->AllocDataBlocks(layout.data_units_count, allocation_type);
    SetExternalMetadataBlocks(metadata, layout, block_numbers);
    StorePayload(layout, metadata, block_numbers, payload);

    REQUIRE(directory_map.insert(name, metadata));
    auto file = directory->GetFile(name);
    REQUIRE(file.has_value());
    return *file;
  }

  uint32_t FreeBlocksCount() {
    auto allocator = quota->GetFreeBlocksAllocator();
    REQUIRE(allocator.has_value());
    return (*allocator)->free_blocks_count();
  }

 private:
  static BlockType AllocationBlockType(FileLayoutCategory category) {
    switch (category) {
      case FileLayoutCategory::Blocks:
        return BlockType::Single;
      case FileLayoutCategory::LargeBlocks:
        return BlockType::Large;
      case FileLayoutCategory::Clusters:
        return BlockType::Cluster;
      case FileLayoutCategory::Inline:
      case FileLayoutCategory::ClusterMetadataBlocks:
        break;
    }
    throw std::invalid_argument("Unexpected test file layout category");
  }

  static BlockType DataBlockType(FileLayoutCategory category) {
    return category == FileLayoutCategory::Blocks ? BlockType::Single : BlockType::Large;
  }

  static size_t DataBlockLog2Size(FileLayoutCategory category, uint8_t block_size_log2) {
    return static_cast<size_t>(block_size_log2) + static_cast<size_t>(log2_size(DataBlockType(category)));
  }

  template <typename T>
  static std::span<T> AlignedMetadataItems(EntryMetadata* metadata, size_t count) {
    auto* metadata_bytes = reinterpret_cast<std::byte*>(metadata);
    const auto metadata_size = sizeof(T) * count;
    auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
    return {reinterpret_cast<T*>(end - metadata_size), count};
  }

  template <typename T>
  static T& LogicalMetadataItem(std::span<T> items, size_t index) {
    return items[items.size() - index - 1];
  }

  static uint32_t DataSizeForBlock(size_t file_size, size_t block_offset, size_t log2_block_size) {
    if (file_size <= block_offset)
      return 0;
    return static_cast<uint32_t>(std::min(size_t{1} << log2_block_size, file_size - block_offset));
  }

  void SetExternalMetadataBlocks(EntryMetadata* metadata,
                                 const FileLayout& layout,
                                 std::span<const uint32_t> block_numbers) {
    REQUIRE(layout.data_units_count == block_numbers.size());
    if (layout.category == FileLayoutCategory::Blocks || layout.category == FileLayoutCategory::LargeBlocks) {
      auto items = AlignedMetadataItems<DataBlockMetadata>(metadata, layout.data_units_count);
      for (size_t i = 0; i < block_numbers.size(); ++i)
        LogicalMetadataItem(items, i).block_number = block_numbers[i];
      return;
    }

    REQUIRE(layout.category == FileLayoutCategory::Clusters);
    auto items = AlignedMetadataItems<DataBlocksClusterMetadata>(metadata, layout.data_units_count);
    for (size_t i = 0; i < block_numbers.size(); ++i)
      LogicalMetadataItem(items, i).block_number = block_numbers[i];
  }

  uint32_t DataBlockNumber(const FileLayout& layout, std::span<const uint32_t> allocation_blocks, size_t block_offset) {
    if (layout.category == FileLayoutCategory::Blocks || layout.category == FileLayoutCategory::LargeBlocks) {
      return allocation_blocks[block_offset >> DataBlockLog2Size(layout.category, quota->block_size_log2())];
    }

    REQUIRE(layout.category == FileLayoutCategory::Clusters);
    const auto cluster_log2_size =
        static_cast<size_t>(quota->block_size_log2()) + static_cast<size_t>(log2_size(BlockType::Cluster));
    const auto large_block_log2_size = DataBlockLog2Size(layout.category, quota->block_size_log2());
    const auto cluster_index = block_offset >> cluster_log2_size;
    const auto block_index_in_cluster = (block_offset - (cluster_index << cluster_log2_size)) >> large_block_log2_size;
    return allocation_blocks[cluster_index] +
           static_cast<uint32_t>(block_index_in_cluster << log2_size(BlockType::Large));
  }

  void StoreDataBlock(uint32_t area_block_number, std::span<const std::byte> data) {
    std::vector<std::byte> aligned_data(
        div_ceil(data.size(), test_device->device()->SectorSize()) * test_device->device()->SectorSize(), std::byte{0});
    std::ranges::copy(data, aligned_data.begin());
    test_device->blocks_[quota->to_physical_block_number(area_block_number)] = std::move(aligned_data);
  }

  void StorePayload(const FileLayout& layout,
                    EntryMetadata* metadata,
                    std::span<const uint32_t> allocation_blocks,
                    std::span<const std::byte> payload) {
    (void)metadata;
    const auto data_block_log2_size = DataBlockLog2Size(layout.category, quota->block_size_log2());
    const auto data_block_size = size_t{1} << data_block_log2_size;
    for (size_t block_offset = 0; block_offset < payload.size(); block_offset += data_block_size) {
      const auto data_size = DataSizeForBlock(payload.size(), block_offset, data_block_log2_size);
      StoreDataBlock(DataBlockNumber(layout, allocation_blocks, block_offset),
                     payload.subspan(block_offset, data_size));
    }
  }
};

std::string EntryName(uint32_t index) {
  auto name = std::to_string(index);
  if (name.size() < 5)
    name.insert(name.begin(), 5 - name.size(), '0');
  return name;
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

std::vector<std::byte> Pattern(size_t size, uint8_t seed) {
  std::vector<std::byte> data(size);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = std::byte{static_cast<uint8_t>(seed + i)};
  return data;
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
                 "File resize keeps existing file handles valid after inline metadata reallocation",
                 "[file][resize]") {
  const auto small_inline_capacity =
      (size_t{1} << 6) - FileLayout::BaseMetadataSize(static_cast<uint8_t>(kTestFilename.size()));
  std::vector<std::byte> initial(small_inline_capacity, std::byte{0x2a});
  auto first_file = InsertInlineFile(kTestFilename, initial);
  auto second_file = directory->GetFile(kTestFilename);
  REQUIRE(second_file.has_value());

  first_file->Resize(small_inline_capacity + 1);

  auto expected = initial;
  expected.push_back(std::byte{0});
  CHECK((*second_file)->Size() == expected.size());
  CHECK(ReadFile(*second_file, expected.size()) == expected);

  constexpr std::array replacement{std::byte{0xf0}};
  WriteFile(*second_file, replacement, small_inline_capacity);
  expected.back() = replacement.front();
  CHECK(ReadFile(first_file, expected.size()) == expected);
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

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize grows category 1 by allocating another single block",
                 "[file][resize]") {
  const auto block_size = quota->block_size();
  auto initial = Pattern(3 * block_size, 0x10);
  auto file = InsertExternalFile(FileLayoutCategory::Blocks, kTestFilename, initial);
  auto original_metadata = FindMetadata(kTestFilename);
  const auto free_before = FreeBlocksCount();

  file->Resize(3 * block_size + 1);

  auto updated_metadata = FindMetadata(kTestFilename);
  CHECK(updated_metadata != original_metadata);
  CHECK(file->Size() == 3 * block_size + 1);
  CHECK(file->SizeOnDisk() == 4 * block_size);
  CHECK(updated_metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(FreeBlocksCount() == free_before - 1);

  auto expected = initial;
  expected.push_back(std::byte{0});
  CHECK(ReadFile(file, expected.size()) == expected);

  const auto replacement = Bytes({0xa1});
  WriteFile(file, replacement, 3 * block_size);
  std::ranges::copy(replacement, expected.begin() + 3 * block_size);
  CHECK(ReadFile(file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize shrinks category 1 and frees removed single blocks",
                 "[file][resize]") {
  const auto block_size = quota->block_size();
  auto initial = Pattern(4 * block_size + 17, 0x20);
  auto file = InsertExternalFile(FileLayoutCategory::Blocks, kTestFilename, initial);
  const auto free_before = FreeBlocksCount();

  file->Resize(2 * block_size + 9);

  CHECK(file->Size() == 2 * block_size + 9);
  CHECK(file->SizeOnDisk() == 3 * block_size);
  CHECK(FreeBlocksCount() == free_before + 2);

  initial.resize(2 * block_size + 9);
  CHECK(ReadFile(file, initial.size()) == initial);

  const auto replacement = Bytes({0xb1, 0xb2, 0xb3, 0xb4});
  WriteFile(file, replacement, 2 * block_size + 1);
  std::ranges::copy(replacement, initial.begin() + 2 * block_size + 1);
  CHECK(ReadFile(file, initial.size()) == initial);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows and shrinks category 2", "[file][resize]") {
  const auto block_size = quota->block_size();
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  auto initial = Pattern(large_block_size + 23, 0x30);
  auto file = InsertExternalFile(FileLayoutCategory::LargeBlocks, kTestFilename, initial);
  const auto free_before_grow = FreeBlocksCount();

  file->Resize(2 * large_block_size + 5);

  CHECK(file->Size() == 2 * large_block_size + 5);
  CHECK(file->SizeOnDisk() == 3 * large_block_size);
  CHECK(FreeBlocksCount() == free_before_grow - (uint32_t{1} << log2_size(BlockType::Large)));
  auto expected = initial;
  expected.resize(2 * large_block_size + 5, std::byte{0});
  CHECK(ReadFile(file, expected.size()) == expected);
  const auto grow_replacement = Bytes({0xc1, 0xc2, 0xc3, 0xc4, 0xc5});
  WriteFile(file, grow_replacement, 2 * large_block_size);
  std::ranges::copy(grow_replacement, expected.begin() + 2 * large_block_size);
  CHECK(ReadFile(file, expected.size()) == expected);

  const auto free_before_shrink = FreeBlocksCount();
  file->Resize(large_block_size + 11);

  CHECK(file->Size() == large_block_size + 11);
  CHECK(file->SizeOnDisk() == 2 * large_block_size);
  CHECK(FreeBlocksCount() == free_before_shrink + (uint32_t{1} << log2_size(BlockType::Large)));
  expected.resize(large_block_size + 11);
  CHECK(ReadFile(file, expected.size()) == expected);
  const auto shrink_replacement = Bytes({0xd1, 0xd2, 0xd3, 0xd4});
  WriteFile(file, shrink_replacement, large_block_size + 3);
  std::ranges::copy(shrink_replacement, expected.begin() + large_block_size + 3);
  CHECK(ReadFile(file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows and shrinks category 3", "[file][resize]") {
  const auto block_size = quota->block_size();
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  const auto cluster_size = block_size << log2_size(BlockType::Cluster);
  auto initial = Pattern(large_block_size + 31, 0x40);
  auto file = InsertExternalFile(FileLayoutCategory::Clusters, kTestFilename, initial);
  const auto free_before_grow = FreeBlocksCount();

  file->Resize(cluster_size + 13);

  CHECK(file->Size() == cluster_size + 13);
  CHECK(file->SizeOnDisk() == 2 * cluster_size);
  CHECK(FreeBlocksCount() == free_before_grow - (uint32_t{1} << log2_size(BlockType::Cluster)));
  auto expected = initial;
  expected.resize(cluster_size + 13, std::byte{0});
  CHECK(ReadFile(file, expected.size()) == expected);
  const auto grow_replacement = Bytes({0xe1, 0xe2, 0xe3, 0xe4});
  WriteFile(file, grow_replacement, cluster_size + 4);
  std::ranges::copy(grow_replacement, expected.begin() + cluster_size + 4);
  CHECK(ReadFile(file, expected.size()) == expected);

  const auto free_before_shrink = FreeBlocksCount();
  file->Resize(large_block_size + 19);

  CHECK(file->Size() == large_block_size + 19);
  CHECK(file->SizeOnDisk() == cluster_size);
  CHECK(FreeBlocksCount() == free_before_shrink + (uint32_t{1} << log2_size(BlockType::Cluster)));
  expected.resize(large_block_size + 19);
  CHECK(ReadFile(file, expected.size()) == expected);
  const auto shrink_replacement = Bytes({0xf1, 0xf2, 0xf3, 0xf4});
  WriteFile(file, shrink_replacement, large_block_size + 7);
  std::ranges::copy(shrink_replacement, expected.begin() + large_block_size + 7);
  CHECK(ReadFile(file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize grows category 1 within the allocated block and zero fills",
                 "[file][resize]") {
  const auto inline_capacity = FileLayout::InlineCapacity(static_cast<uint8_t>(kTestFilename.size()));
  auto initial = Pattern(inline_capacity + 8, 0x50);
  auto file = InsertExternalFile(FileLayoutCategory::Blocks, kTestFilename, initial);
  const auto free_before = FreeBlocksCount();

  file->Resize(initial.size() + 8);

  CHECK(file->Size() == initial.size() + 8);
  CHECK(file->SizeOnDisk() == quota->block_size());
  CHECK(FreeBlocksCount() == free_before);
  auto expected = initial;
  expected.resize(initial.size() + 8, std::byte{0});
  CHECK(ReadFile(file, expected.size()) == expected);

  const auto replacement = Bytes({0x91, 0x92, 0x93, 0x94});
  WriteFile(file, replacement, initial.size() + 2);
  std::ranges::copy(replacement, expected.begin() + initial.size() + 2);
  CHECK(ReadFile(file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize rejects external category transitions for now", "[file][resize]") {
  const auto block_size = quota->block_size();
  auto initial = Pattern(5 * block_size, 0x60);
  auto file = InsertExternalFile(FileLayoutCategory::Blocks, kTestFilename, initial);

  RequireResizeError(file, 5 * block_size + 1, WfsError::kUnsupportedFileResize);

  CHECK(file->Size() == 5 * block_size);
  CHECK(file->SizeOnDisk() == 5 * block_size);
  CHECK(ReadFile(file, initial.size()) == initial);
}
