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
#include "free_blocks_allocator.h"
#include "quota_area.h"
#include "structs.h"
#include "utils.h"
#include "utils/test_fixtures.h"

namespace {
constexpr std::string_view kTestFilename = "file";
constexpr std::array kInitialData{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
constexpr std::array kReplacementData{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};
constexpr std::array kPostResizeData{std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88}};

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
    return CreateFile(name,
                      FileLayout::Calculate(0, file_size, static_cast<uint8_t>(name.size()), quota->block_size_log2()));
  }

  TestFile CreateDataUnitFile(std::string_view name, uint32_t file_size, std::span<const std::byte> data) {
    return CreateDataUnitFile(
        name, FileLayout::Calculate(0, file_size, static_cast<uint8_t>(name.size()), quota->block_size_log2()), data);
  }

  TestFile CreateDataUnitFile(std::string_view name, const FileLayout& layout, std::span<const std::byte> data) {
    auto test_file = CreateFile(name, layout);
    AssignDataBlocks(test_file.metadata);
    StoreFileData(test_file.metadata, data);
    return test_file;
  }

  EntryMetadata* StoredMetadata(std::string_view name) {
    auto it = directory_map->find(name);
    REQUIRE(!it.is_end());
    return (*it).metadata.get_mutable();
  }

  uint32_t FreeBlocksCount() { return (*quota->GetFreeBlocksAllocator())->free_blocks_count(); }

 private:
  static FileLayoutCategory Category(const EntryMetadata* metadata) {
    return FileLayout::CategoryFromValue(metadata->size_category.value());
  }

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
    throw std::logic_error("Unexpected test file layout category");
  }

  static BlockType DataBlockType(FileLayoutCategory category) {
    switch (category) {
      case FileLayoutCategory::Blocks:
        return BlockType::Single;
      case FileLayoutCategory::LargeBlocks:
      case FileLayoutCategory::Clusters:
        return BlockType::Large;
      case FileLayoutCategory::Inline:
      case FileLayoutCategory::ClusterMetadataBlocks:
        break;
    }
    throw std::logic_error("Unexpected test file layout category");
  }

  template <typename T>
  std::span<T> AlignedMetadataItems(EntryMetadata* metadata, size_t count) {
    auto* metadata_bytes = reinterpret_cast<std::byte*>(metadata);
    const auto metadata_size = sizeof(T) * count;
    auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
    return {reinterpret_cast<T*>(end - metadata_size), count};
  }

  void AssignDataBlocks(EntryMetadata* metadata) {
    const auto category = Category(metadata);
    const auto metadata_items_count =
        FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), quota->block_size_log2());
    auto allocated_blocks = quota->AllocDataBlocks(metadata_items_count, AllocationBlockType(category));
    REQUIRE(allocated_blocks.has_value());

    if (category == FileLayoutCategory::Blocks || category == FileLayoutCategory::LargeBlocks) {
      auto block_refs = AlignedMetadataItems<DataBlockMetadata>(metadata, metadata_items_count);
      for (size_t i = 0; i < allocated_blocks->size(); ++i)
        block_refs[allocated_blocks->size() - i - 1].block_number = (*allocated_blocks)[i];
      return;
    }

    auto cluster_refs = AlignedMetadataItems<DataBlocksClusterMetadata>(metadata, metadata_items_count);
    for (size_t i = 0; i < allocated_blocks->size(); ++i)
      cluster_refs[allocated_blocks->size() - i - 1].block_number = (*allocated_blocks)[i];
  }

  void StoreDataBlock(uint32_t area_block_number, std::span<const std::byte> data) {
    std::vector<std::byte> aligned_data(
        div_ceil(data.size(), test_device->device()->SectorSize()) * test_device->device()->SectorSize(), std::byte{0});
    std::ranges::copy(data, aligned_data.begin());
    test_device->blocks_[quota->to_physical_block_number(area_block_number)] = std::move(aligned_data);
  }

  void StoreFileData(EntryMetadata* metadata, std::span<const std::byte> data) {
    const auto category = Category(metadata);
    const auto data_block_size = size_t{1} << (quota->block_size_log2() + log2_size(DataBlockType(category)));

    if (category == FileLayoutCategory::Blocks || category == FileLayoutCategory::LargeBlocks) {
      auto block_refs = AlignedMetadataItems<DataBlockMetadata>(
          metadata, FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), quota->block_size_log2()));
      for (size_t i = 0, offset = 0; offset < data.size(); ++i, offset += data_block_size) {
        const auto chunk_size = std::min(data_block_size, data.size() - offset);
        StoreDataBlock(block_refs[block_refs.size() - i - 1].block_number.value(), data.subspan(offset, chunk_size));
      }
      return;
    }

    constexpr size_t kLargeBlocksPerCluster = size_t{1} << log2_size(BlockType::Cluster) >> log2_size(BlockType::Large);
    auto cluster_refs = AlignedMetadataItems<DataBlocksClusterMetadata>(
        metadata, FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), quota->block_size_log2()));
    for (size_t i = 0, offset = 0; offset < data.size(); ++i, offset += data_block_size) {
      const auto cluster_index = i / kLargeBlocksPerCluster;
      const auto large_block_index = i % kLargeBlocksPerCluster;
      const auto chunk_size = std::min(data_block_size, data.size() - offset);
      const auto cluster_block_number = cluster_refs[cluster_refs.size() - cluster_index - 1].block_number.value();
      StoreDataBlock(cluster_block_number + static_cast<uint32_t>(large_block_index << log2_size(BlockType::Large)),
                     data.subspan(offset, chunk_size));
    }
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

std::vector<std::byte> DataPattern(size_t size) {
  std::vector<std::byte> data(size);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = std::byte{static_cast<unsigned char>((i * 131 + 17) & 0xff)};
  return data;
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
  const auto target_layout =
      FileLayout::Calculate(0, kGrownSize, static_cast<uint8_t>(kTestFilename.size()), quota->block_size_log2());
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

TEST_CASE_METHOD(FileResizeFixture, "File device write appends to empty file", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, 0);

  CHECK(WriteFile(test_file.file, kReplacementData, 0) == kReplacementData.size());

  auto* metadata = StoredMetadata(kTestFilename);
  const std::vector<std::byte> expected(kReplacementData.begin(), kReplacementData.end());
  CHECK(test_file.file->Size() == expected.size());
  CHECK(test_file.file->SizeOnDisk() == expected.size());
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device write appends to non-empty file", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());

  CHECK(WriteFile(test_file.file, kReplacementData, kInitialData.size()) == kReplacementData.size());

  std::vector<std::byte> expected(kInitialData.begin(), kInitialData.end());
  expected.insert(expected.end(), kReplacementData.begin(), kReplacementData.end());
  CHECK(test_file.file->Size() == expected.size());
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device write overwrites without resizing", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());

  CHECK(WriteFile(test_file.file, kReplacementData, 0) == kReplacementData.size());

  const std::vector<std::byte> expected(kReplacementData.begin(), kReplacementData.end());
  CHECK(test_file.file->Size() == expected.size());
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device write grows within current category", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = block_size + static_cast<uint32_t>(kInitialData.size());
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  const auto free_blocks_before = FreeBlocksCount();

  CHECK(WriteFile(test_file.file, kReplacementData, initial_size) == kReplacementData.size());

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.insert(expected.end(), kReplacementData.begin(), kReplacementData.end());
  CHECK(test_file.file->Size() == expected.size());
  CHECK(test_file.file->SizeOnDisk() == 2 * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(FreeBlocksCount() == free_blocks_before);
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device write grows across categories", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto initial_size = 5 * block_size;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  CHECK(WriteFile(test_file.file, kReplacementData, initial_size) == kReplacementData.size());

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.insert(expected.end(), kReplacementData.begin(), kReplacementData.end());
  CHECK(test_file.file->Size() == expected.size());
  CHECK(test_file.file->SizeOnDisk() == large_block_blocks_count * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device write appends after truncate", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = 2 * block_size + static_cast<uint32_t>(kInitialData.size());
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  test_file.file->Resize(block_size);
  CHECK(WriteFile(test_file.file, kReplacementData, block_size) == kReplacementData.size());

  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + block_size};
  expected.insert(expected.end(), kReplacementData.begin(), kReplacementData.end());
  CHECK(test_file.file->Size() == expected.size());
  CHECK(ReadFile(test_file.file, expected.size()) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device seek allows EOF but rejects beyond EOF", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());

  File::file_device device(test_file.file);
  CHECK(device.seek(0, std::ios_base::end) == static_cast<boost::iostreams::stream_offset>(kInitialData.size()));
  CHECK(device.seek(static_cast<boost::iostreams::stream_offset>(kInitialData.size()), std::ios_base::beg) ==
        static_cast<boost::iostreams::stream_offset>(kInitialData.size()));
  CHECK_THROWS_AS(device.seek(1, std::ios_base::end), std::ios_base::failure);
  CHECK_THROWS_AS(
      device.seek(static_cast<boost::iostreams::stream_offset>(kInitialData.size() + 1), std::ios_base::beg),
      std::ios_base::failure);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize truncates inline file to zero", "[file-resize][unit]") {
  constexpr uint32_t kInitialSize = 80;
  auto test_file = CreateFile(kTestFilename, kInitialSize);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));

  const auto target_layout =
      FileLayout::Calculate(kInitialSize, 0, static_cast<uint8_t>(kTestFilename.size()), quota->block_size_log2());

  test_file.file->Resize(0);

  auto* metadata = StoredMetadata(kTestFilename);
  CHECK(test_file.file->Size() == 0);
  CHECK(test_file.file->SizeOnDisk() == 0);
  CHECK(metadata->metadata_log2_size.value() == target_layout.metadata_log2_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CHECK(InlinePayload(metadata).empty());
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows inline file into category 1", "[file-resize][unit]") {
  auto test_file = CreateFile(kTestFilename, static_cast<uint32_t>(kInitialData.size()));
  std::ranges::copy(kInitialData, InlinePayload(test_file.metadata).begin());
  const auto target_size = FileLayout::InlineCapacity(static_cast<uint8_t>(kTestFilename.size())) + 1;
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{kInitialData.begin(), kInitialData.end()};
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == quota->block_size());
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(FreeBlocksCount() == free_blocks_before - 1);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize shrink keeps current category when target still fits",
                 "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = 2 * block_size + 8;
  const auto target_size = block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == 2 * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 1 into inline data", "[file-resize][unit]") {
  const auto initial_size = FileLayout::InlineCapacity(static_cast<uint8_t>(kTestFilename.size())) + 1;
  const auto target_size = static_cast<uint32_t>(kInitialData.size());
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == target_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CHECK(FreeBlocksCount() == free_blocks_before + 1);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 1 into category 2", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto initial_size = 5 * block_size;
  const auto target_size = initial_size + 1;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == large_block_blocks_count * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  CHECK(FreeBlocksCount() == free_blocks_before + 5 - large_block_blocks_count);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 2 into category 1", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto initial_size = 5 * block_size + 1;
  const auto target_size = block_size;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(FreeBlocksCount() == free_blocks_before + large_block_blocks_count - 1);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 2 without lowering category", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto large_block_size = large_block_blocks_count * block_size;
  const auto initial_size = block_size + 1;
  const auto target_size = block_size + 4;
  const auto layout = FileLayout::Calculate(initial_size, initial_size, static_cast<uint8_t>(kTestFilename.size()),
                                            quota->block_size_log2(), FileLayoutCategory::LargeBlocks);
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, layout, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == large_block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  CHECK(FreeBlocksCount() == free_blocks_before);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 2 into category 3", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto cluster_blocks_count = uint32_t{1} << log2_size(BlockType::Cluster);
  const auto large_block_size = large_block_blocks_count * block_size;
  const auto initial_size = 5 * large_block_size;
  const auto target_size = initial_size + 1;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == cluster_blocks_count * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  CHECK(FreeBlocksCount() == free_blocks_before + 5 * large_block_blocks_count - cluster_blocks_count);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 3 into category 2", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto cluster_blocks_count = uint32_t{1} << log2_size(BlockType::Cluster);
  const auto large_block_size = large_block_blocks_count * block_size;
  const auto initial_size = 5 * large_block_size + 1;
  const auto target_size = large_block_size;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == large_block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  CHECK(FreeBlocksCount() == free_blocks_before + cluster_blocks_count - large_block_blocks_count);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize can grow across more than one category", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_blocks_count = uint32_t{1} << log2_size(BlockType::Large);
  const auto cluster_blocks_count = uint32_t{1} << log2_size(BlockType::Cluster);
  const auto large_block_size = large_block_blocks_count * block_size;
  const auto initial_size = block_size + 4;
  const auto target_size = 5 * large_block_size + 1;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == cluster_blocks_count * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  CHECK(FreeBlocksCount() == free_blocks_before + 2 - cluster_blocks_count);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 1 by one block", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = block_size + 4;
  const auto target_size = 2 * block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == 3 * block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(FreeBlocksCount() == free_blocks_before - 1);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize preserves dirty cached data blocks", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = block_size + 4;
  const auto target_size = 2 * block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  {
    File::file_device writer(test_file.file);
    const auto wrote = writer.write(reinterpret_cast<const char*>(kReplacementData.data()),
                                    static_cast<std::streamsize>(kReplacementData.size()));
    REQUIRE(wrote == static_cast<std::streamsize>(kReplacementData.size()));

    test_file.file->Resize(target_size);
  }

  auto expected = initial_data;
  std::ranges::copy(kReplacementData, expected.begin());
  expected.resize(target_size, std::byte{0});
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize lets existing file devices reload detached blocks",
                 "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = block_size + 4;
  const auto target_size = 2 * block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  {
    std::array<std::byte, kPostResizeData.size()> data{};
    File::file_device writer(test_file.file);
    const auto read = writer.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    REQUIRE(read == static_cast<std::streamsize>(data.size()));
    writer.seek(0, std::ios_base::beg);

    test_file.file->Resize(target_size);

    const auto wrote = writer.write(reinterpret_cast<const char*>(kPostResizeData.data()),
                                    static_cast<std::streamsize>(kPostResizeData.size()));
    REQUIRE(wrote == static_cast<std::streamsize>(kPostResizeData.size()));
  }

  auto expected = initial_data;
  std::ranges::copy(kPostResizeData, expected.begin());
  expected.resize(target_size, std::byte{0});
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device uses current layout after category growth", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = 5 * block_size;
  const auto target_size = initial_size + 1;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  {
    File::file_device writer(test_file.file);
    test_file.file->Resize(target_size);
    REQUIRE(StoredMetadata(kTestFilename)->size_category.value() ==
            FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));

    writer.seek(block_size, std::ios_base::beg);
    const auto wrote = writer.write(reinterpret_cast<const char*>(kPostResizeData.data()),
                                    static_cast<std::streamsize>(kPostResizeData.size()));
    REQUIRE(wrote == static_cast<std::streamsize>(kPostResizeData.size()));
  }

  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  std::ranges::copy(kPostResizeData, expected.begin() + block_size);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File device uses current layout after category shrink", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = 5 * block_size + 1;
  const auto target_size = block_size;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));

  {
    File::file_device writer(test_file.file);
    test_file.file->Resize(target_size);
    REQUIRE(StoredMetadata(kTestFilename)->size_category.value() ==
            FileLayout::CategoryValue(FileLayoutCategory::Blocks));

    const auto wrote = writer.write(reinterpret_cast<const char*>(kPostResizeData.data()),
                                    static_cast<std::streamsize>(kPostResizeData.size()));
    REQUIRE(wrote == static_cast<std::streamsize>(kPostResizeData.size()));
  }

  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  std::ranges::copy(kPostResizeData, expected.begin());
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture,
                 "File resize failure keeps dirty writes in blocks that would be dropped",
                 "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = 2 * block_size + 4;
  const auto target_size = block_size;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));

  auto it = directory_map->find(kTestFilename);
  REQUIRE(!it.is_end());
  auto synthetic_file =
      std::make_shared<File>(Entry::CreateSyntheticEntryHandle(std::string{kTestFilename}, (*it).metadata), quota);

  File::file_device writer(synthetic_file);
  writer.seek(block_size, std::ios_base::beg);
  const auto wrote = writer.write(reinterpret_cast<const char*>(kReplacementData.data()),
                                  static_cast<std::streamsize>(kReplacementData.size()));
  REQUIRE(wrote == static_cast<std::streamsize>(kReplacementData.size()));

  CHECK_THROWS_AS(synthetic_file->Resize(target_size), std::logic_error);

  std::array<std::byte, kReplacementData.size()> observed{};
  writer.seek(block_size, std::ios_base::beg);
  const auto read =
      writer.read(reinterpret_cast<char*>(observed.data()), static_cast<std::streamsize>(observed.size()));
  REQUIRE(read == static_cast<std::streamsize>(observed.size()));
  CHECK(observed == kReplacementData);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 1 to one block", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = 2 * block_size + 4;
  const auto target_size = block_size;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Blocks));
  CHECK(FreeBlocksCount() == free_blocks_before + 2);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 1 partial last block", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = block_size + 4;
  const auto target_size = block_size + 8;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == 2 * block_size);
  CHECK(FreeBlocksCount() == free_blocks_before);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 1 partial last block", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto initial_size = block_size;
  const auto target_size = block_size - 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == block_size);
  CHECK(FreeBlocksCount() == free_blocks_before);
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 2 by one large block", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  const auto initial_size = 5 * block_size + 4;
  const auto target_size = large_block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == 2 * large_block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  CHECK(FreeBlocksCount() == free_blocks_before - (uint32_t{1} << log2_size(BlockType::Large)));
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 2 by one large block", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  const auto initial_size = large_block_size + 4;
  const auto target_size = 5 * block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == large_block_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::LargeBlocks));
  CHECK(FreeBlocksCount() == free_blocks_before + (uint32_t{1} << log2_size(BlockType::Large)));
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize grows category 3 by one cluster", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  const auto cluster_size = block_size << log2_size(BlockType::Cluster);
  const auto initial_size = 5 * large_block_size + 4;
  const auto target_size = cluster_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = initial_data;
  expected.resize(target_size, std::byte{0});
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == 2 * cluster_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  CHECK(FreeBlocksCount() == free_blocks_before - (uint32_t{1} << log2_size(BlockType::Cluster)));
  CHECK(ReadFile(test_file.file, target_size) == expected);
}

TEST_CASE_METHOD(FileResizeFixture, "File resize shrinks category 3 by one cluster", "[file-resize][unit]") {
  const auto block_size = static_cast<uint32_t>(quota->block_size());
  const auto large_block_size = block_size << log2_size(BlockType::Large);
  const auto cluster_size = block_size << log2_size(BlockType::Cluster);
  const auto initial_size = cluster_size + 4;
  const auto target_size = 5 * large_block_size + 4;
  auto initial_data = DataPattern(initial_size);
  auto test_file = CreateDataUnitFile(kTestFilename, initial_size, initial_data);
  REQUIRE(test_file.metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  const auto free_blocks_before = FreeBlocksCount();

  test_file.file->Resize(target_size);

  auto* metadata = StoredMetadata(kTestFilename);
  auto expected = std::vector<std::byte>{initial_data.begin(), initial_data.begin() + target_size};
  CHECK(test_file.file->Size() == target_size);
  CHECK(test_file.file->SizeOnDisk() == cluster_size);
  CHECK(metadata->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Clusters));
  CHECK(FreeBlocksCount() == free_blocks_before + (uint32_t{1} << log2_size(BlockType::Cluster)));
  CHECK(ReadFile(test_file.file, target_size) == expected);
}
