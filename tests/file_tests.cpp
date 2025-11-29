/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>
#include <ranges>
#include <span>

#include <wfslib/errors.h>
#include <wfslib/file.h>
#include <wfslib/wfs_device.h>

#include "block.h"
#include "file_internal.h"
#include "quota_area.h"

#include "utils/test_blocks_device.h"

namespace {

std::shared_ptr<Block> create_metadata_block(uint8_t log2_size) {
  auto block = Block::CreateDetached(std::vector<std::byte>(1u << log2_size, std::byte{0}));
  auto* metadata = block->get_mutable_object<EntryMetadata>(0);
  metadata->metadata_log2_size = log2_size;
  metadata->filename_length = 0;
  metadata->case_bitmap = 0;
  return block;
}

void fill_span(std::span<std::byte> data, char base) {
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = std::byte{static_cast<unsigned char>(base + static_cast<char>(i % 26))};
  }
}

TEST_CASE("File data is preserved across storage migrations") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto wfs_device = *WfsDevice::Create(device);
  auto quota = wfs_device->GetRootArea();

  const size_t block_size = size_t{1} << quota->block_size_log2();
  const size_t large_block_size =
      size_t{1} << file_internal::block_capacity_log2(quota->block_size_log2(), BlockType::Large);
  const size_t cluster_size =
      size_t{1} << file_internal::block_capacity_log2(quota->block_size_log2(), BlockType::Cluster);

  auto metadata_block = create_metadata_block(/*log2_size=*/12);
  auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
  metadata->size_category = static_cast<uint8_t>(file_internal::DataType::InMetadata);
  metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
  auto inline_capacity =
      (size_t{1} << metadata->metadata_log2_size.value()) - metadata->size();
  metadata->size_on_disk = static_cast<uint32_t>(inline_capacity);
  metadata->file_size = 0;

  Entry::MetadataRef metadata_ref{metadata_block, 0};
  auto file = std::make_shared<File>("migration", metadata_ref, quota);
  File::file_device io(file);

  std::vector<char> pattern(cluster_size * 5 + 256);
  std::iota(pattern.begin(), pattern.end(), 0);
  const size_t initial_write = inline_capacity - 64;

  auto expect_storage = [&](file_internal::DataType expected) {
    CHECK(static_cast<file_internal::DataType>(metadata->size_category.value()) == expected);
  };

  auto verify_prefix = [&](size_t count) {
    std::vector<char> buffer(count);
    REQUIRE(io.seek(0, std::ios_base::beg) == 0);
    auto read = io.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    REQUIRE(read == static_cast<std::streamsize>(buffer.size()));
    CHECK(std::ranges::equal(buffer, std::span<const char>(pattern.data(), buffer.size())));
  };

  // Seed inline data.
  REQUIRE(io.seek(0, std::ios_base::beg) == 0);
  REQUIRE(io.write(pattern.data(), static_cast<std::streamsize>(initial_write)) ==
          static_cast<std::streamsize>(initial_write));
  CHECK(file->Size() == initial_write);
  expect_storage(file_internal::DataType::InMetadata);

  // Promote to single-block storage.
  size_t single_target = inline_capacity + 100;
  file->EnsureSize(single_target);
  expect_storage(file_internal::DataType::SingleBlocks);
  CHECK(file->Size() == single_target);
  verify_prefix(initial_write);

  // Promote to large-block storage.
  size_t large_target = block_size * 5 + 10;
  file->EnsureSize(large_target);
  expect_storage(file_internal::DataType::LargeBlocks);
  CHECK(file->Size() == large_target);
  verify_prefix(initial_write);

  // Promote to clustered storage.
  size_t cluster_target = large_block_size * 5 + 10;
  file->EnsureSize(cluster_target);
  expect_storage(file_internal::DataType::ClusterBlocks);
  CHECK(file->Size() == cluster_target);
  verify_prefix(initial_write);

  // Promote to extended clustered storage.
  size_t extended_target = cluster_size * 4 + 123;
  file->EnsureSize(extended_target);
  expect_storage(file_internal::DataType::ExtendedClusterBlocks);
  CHECK(file->Size() == extended_target);
  verify_prefix(initial_write);

  // Demote back to inline by truncating to small size.
  constexpr size_t kInlineTarget = 32;
  file->Truncate(kInlineTarget);
  expect_storage(file_internal::DataType::InMetadata);
  CHECK(file->Size() == kInlineTarget);
  verify_prefix(std::min(initial_write, kInlineTarget));
}

template <typename T>
std::span<T> metadata_tail_span(std::shared_ptr<Block> block, EntryMetadata* metadata, size_t count) {
  if (count == 0)
    return {};
  auto total = size_t{1} << metadata->metadata_log2_size.value();
  auto* base = reinterpret_cast<std::byte*>(block->mutable_data().data());
  auto* end = base + total;
  auto* start = end - count * sizeof(T);
  return {reinterpret_cast<T*>(start), count};
}

void write_bytes(std::span<std::byte> dst, std::string_view data) {
  for (size_t i = 0; i < data.size(); ++i) {
    dst[i] = std::byte{static_cast<unsigned char>(data[i])};
  }
}

}  // namespace

TEST_CASE("FileTests") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto wfs_device = *WfsDevice::Create(device);
  auto quota = wfs_device->GetRootArea();

  SECTION("Inline payload read/write and zero-fill") {
    constexpr size_t kInitialSize = 8;

    auto metadata_block = create_metadata_block(/*log2_size=*/7);
    auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
    metadata->size_category = 0;
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    auto inline_capacity =
        (size_t{1} << metadata->metadata_log2_size.value()) - metadata->size();
    metadata->size_on_disk = static_cast<uint32_t>(inline_capacity);
    metadata->file_size = static_cast<uint32_t>(kInitialSize);

    auto payload = metadata_block->mutable_data().subspan(metadata->size(), inline_capacity);
    std::ranges::fill(payload, std::byte{0});
    write_bytes(payload, "WFSLIB!!");

    Entry::MetadataRef metadata_ref{metadata_block, 0};
    auto file = std::make_shared<File>("inline", metadata_ref, quota);
    File::file_device io(file);

    SECTION("Initial read matches inline payload") {
      std::vector<char> buffer(inline_capacity);
      REQUIRE(io.seek(0, std::ios_base::beg) == 0);
      auto read = io.read(buffer.data(), buffer.size());
      CHECK(read == static_cast<std::streamsize>(kInitialSize));
      CHECK(std::string_view(buffer.data(), kInitialSize) == "WFSLIB!!");
    }

    SECTION("EnsureSize within inline capacity zero-fills extension") {
      file->EnsureSize(12);
      CHECK(file->Size() == 12);
      std::vector<char> buffer(inline_capacity);
      REQUIRE(io.seek(0, std::ios_base::beg) == 0);
      auto read = io.read(buffer.data(), buffer.size());
      CHECK(read == 12);
      CHECK(std::string_view(buffer.data(), 8) == "WFSLIB!!");
      CHECK(std::all_of(buffer.begin() + 8, buffer.begin() + 12, [](char c) { return c == '\0'; }));
    }

    SECTION("Writes update inline payload and resize file") {
      std::vector<char> new_payload(inline_capacity);
      std::ranges::copy(std::string_view("ABCDEFGHijklmnop"), new_payload.begin());
      auto written = io.write(new_payload.data(), static_cast<std::streamsize>(inline_capacity));
      CHECK(written == static_cast<std::streamsize>(inline_capacity));
      CHECK(file->Size() == inline_capacity);

      std::vector<char> verify(inline_capacity);
      REQUIRE(io.seek(0, std::ios_base::beg) == 0);
      auto read = io.read(verify.data(), verify.size());
      CHECK(read == static_cast<std::streamsize>(inline_capacity));
      CHECK(std::ranges::equal(verify, new_payload));
    }
  }

  SECTION("Single block read/write through file_device") {
    constexpr size_t kInitialSize = 100;
    constexpr size_t kReservedSize = 1u << 13;

    auto metadata_block = create_metadata_block(/*log2_size=*/7);
    auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
    metadata->size_category = 1;
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->size_on_disk = static_cast<uint32_t>(kReservedSize);
    metadata->file_size = static_cast<uint32_t>(kInitialSize);

    auto block_numbers =
        throw_if_error(quota->AllocDataBlocks(1, BlockType::Single));
    auto block_entries = metadata_tail_span<DataBlockMetadata>(metadata_block, metadata, block_numbers.size());
    block_entries.front().block_number = block_numbers.front();
    for (auto& hash_byte : block_entries.front().hash) {
      hash_byte = 0;
    }

    auto hash_offset = metadata_block->to_offset(&block_entries.front().hash[0]);
    auto data_block =
        throw_if_error(quota->LoadDataBlock(block_numbers.front(),
                                            static_cast<BlockSize>(quota->block_size_log2()),
                                            BlockType::Single,
                                            static_cast<uint32_t>(kInitialSize),
                                            Block::HashRef{metadata_block, hash_offset},
                                            /*encrypted=*/false,
                                            /*new_block=*/true));
    auto initial_span = data_block->mutable_data().subspan(0, kInitialSize);
    fill_span(initial_span, 'A');
    data_block->Flush();

    Entry::MetadataRef metadata_ref{metadata_block, 0};
    auto file = std::make_shared<File>("regular", metadata_ref, quota);
    File::file_device io(file);

    SECTION("Reads back original block data") {
      std::array<char, kInitialSize> buffer{};
      auto read = io.read(buffer.data(), buffer.size());
      CHECK(read == static_cast<std::streamsize>(kInitialSize));
      for (size_t i = 0; i < kInitialSize; ++i) {
        CHECK(buffer[i] == static_cast<char>('A' + static_cast<char>(i % 26)));
      }
    }

    SECTION("Writes enlarge file within reserved space and persist data") {
      constexpr size_t kExpandedSize = 200;
      std::vector<char> new_data(kExpandedSize);
      for (size_t i = 0; i < new_data.size(); ++i) {
        new_data[i] = static_cast<char>('a' + static_cast<char>(i % 26));
      }

      auto written = io.write(new_data.data(), static_cast<std::streamsize>(new_data.size()));
      CHECK(written == static_cast<std::streamsize>(new_data.size()));
      CHECK(file->Size() == kExpandedSize);

      std::vector<char> verify(kExpandedSize);
      REQUIRE(io.seek(0, std::ios_base::beg) == 0);
      auto read = io.read(verify.data(), static_cast<std::streamsize>(verify.size()));
      CHECK(read == static_cast<std::streamsize>(verify.size()));
      CHECK(std::ranges::equal(verify, new_data));
    }

    SECTION("Truncate shrinks logical file size") {
      file->Truncate(50);
      CHECK(file->Size() == 50);
      CHECK(metadata->size_category.value() == 0);
      CHECK(file->SizeOnDisk() == 50);

      std::array<char, 100> buffer{};
      REQUIRE(io.seek(0, std::ios_base::beg) == 0);
      auto read = io.read(buffer.data(), buffer.size());
      CHECK(read == 50);
      auto second_read = io.read(buffer.data(), buffer.size());
      CHECK(second_read == -1);
    }
  }

  SECTION("EnsureSize promotes inline files to block-backed storage") {
    constexpr uint8_t kLog2Size = 8;
    auto metadata_block = create_metadata_block(kLog2Size);
    auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
    metadata->size_category = 0;
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;

    auto total_bytes = size_t{1} << metadata->metadata_log2_size.value();
    auto inline_capacity = total_bytes - metadata->size();
    const size_t kInitialSize = 40;
    metadata->size_on_disk = static_cast<uint32_t>(inline_capacity);
    metadata->file_size = static_cast<uint32_t>(kInitialSize);

    auto payload = metadata_block->mutable_data().subspan(metadata->size(), inline_capacity);
    fill_span(payload.first(kInitialSize), 'I');

    Entry::MetadataRef metadata_ref{metadata_block, 0};
    auto file = std::make_shared<File>("inline-to-block", metadata_ref, quota);
    File::file_device io(file);

    auto target_size = inline_capacity + 8;
    REQUIRE_NOTHROW(file->EnsureSize(target_size));
    CHECK(metadata->size_category.value() == 1);
    auto block_size = size_t{1} << quota->block_size_log2();
    CHECK(file->SizeOnDisk() == block_size);
    CHECK(file->Size() == target_size);

    std::vector<char> buffer(target_size);
    REQUIRE(io.seek(0, std::ios_base::beg) == 0);
    auto read = io.read(buffer.data(), buffer.size());
    CHECK(read == static_cast<std::streamsize>(target_size));
    for (size_t i = 0; i < kInitialSize; ++i) {
      CHECK(buffer[i] == static_cast<char>('I' + static_cast<char>(i % 26)));
    }
    CHECK(std::all_of(buffer.begin() + kInitialSize, buffer.end(), [](char c) { return c == '\0'; }));
  }

  SECTION("EnsureSize upgrades file category when needed") {
    constexpr size_t kBlocks = 5;
    const size_t block_size = size_t{1} << quota->block_size_log2();

    auto metadata_block = create_metadata_block(/*log2_size=*/8);
    auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
    metadata->size_category = 1;
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->size_on_disk = static_cast<uint32_t>(kBlocks * block_size);
    metadata->file_size = static_cast<uint32_t>(block_size);

    auto block_numbers = throw_if_error(quota->AllocDataBlocks(kBlocks, BlockType::Single));
    auto entries = metadata_tail_span<DataBlockMetadata>(metadata_block, metadata, block_numbers.size());
    for (size_t i = 0; i < block_numbers.size(); ++i) {
      entries[i].block_number = block_numbers[i];
      std::fill(std::begin(entries[i].hash), std::end(entries[i].hash), uint8_be_t{0});
    }

    const size_t kInitialDataSize = block_size;
    auto hash_offset = metadata_block->to_offset(&entries.front().hash[0]);
    auto data_block =
        throw_if_error(quota->LoadDataBlock(block_numbers.front(),
                                            static_cast<BlockSize>(quota->block_size_log2()),
                                            BlockType::Single,
                                            static_cast<uint32_t>(kInitialDataSize),
                                            Block::HashRef{metadata_block, hash_offset},
                                            /*encrypted=*/false,
                                            /*new_block=*/true));
    fill_span(data_block->mutable_data().subspan(0, kInitialDataSize), 'A');
    data_block->Flush();

    Entry::MetadataRef metadata_ref{metadata_block, 0};
    auto file = std::make_shared<File>("category-limits", metadata_ref, quota);

    SECTION("Growth within category succeeds") {
      REQUIRE_NOTHROW(file->EnsureSize(kBlocks * block_size));
      CHECK(file->Size() == kBlocks * block_size);
      CHECK(metadata->size_category.value() == 1);
    }

    SECTION("Crossing into larger category migrates data") {
      const auto large_block_size =
          size_t{1} << (quota->block_size_log2() + log2_size(BlockType::Large));
      const auto target_size = kBlocks * block_size + 1;
      REQUIRE_NOTHROW(file->EnsureSize(target_size));
      CHECK(metadata->size_category.value() == 2);
      CHECK(file->Size() == target_size);
      CHECK(file->SizeOnDisk() == large_block_size);

      auto large_entries = metadata_tail_span<DataBlockMetadata>(metadata_block, metadata, 1);
      const auto& first_large = large_entries.back();
      auto large_hash_offset = metadata_block->to_offset(&first_large.hash[0]);
      auto new_block =
          throw_if_error(quota->LoadDataBlock(first_large.block_number.value(),
                                              static_cast<BlockSize>(quota->block_size_log2()),
                                              BlockType::Large,
                                              static_cast<uint32_t>(target_size),
                                              Block::HashRef{metadata_block, large_hash_offset},
                                              /*encrypted=*/false));
      auto raw = new_block->data().subspan(0, kInitialDataSize);
      for (size_t i = 0; i < kInitialDataSize; ++i) {
        CHECK(static_cast<char>(raw[i]) == static_cast<char>('A' + static_cast<char>(i % 26)));
      }

      auto physical_block = quota->to_physical_block_number(first_large.block_number.value());
      REQUIRE(device->blocks_.contains(physical_block));
      const auto& stored_data = device->blocks_.at(physical_block);
      CHECK(stored_data.size() >= kInitialDataSize);
      for (size_t i = 0; i < kInitialDataSize; ++i) {
        CHECK(static_cast<char>(stored_data[i]) == static_cast<char>('A' + static_cast<char>(i % 26)));
      }

      std::vector<char> buffer(target_size);
      auto read_log_before = device->read_log_.size();
      File::file_device io(file);
      REQUIRE(io.seek(0, std::ios_base::beg) == 0);
      auto read = io.read(buffer.data(), buffer.size());
      CHECK(read == static_cast<std::streamsize>(target_size));
      REQUIRE(device->read_log_.size() > read_log_before);
      CHECK(device->read_log_[read_log_before] == physical_block);
      for (size_t i = 0; i < kInitialDataSize; ++i) {
        CHECK(buffer[i] == static_cast<char>('A' + static_cast<char>(i % 26)));
      }
      CHECK(std::all_of(buffer.begin() + kInitialDataSize, buffer.end(), [](char c) { return c == '\0'; }));
    }
  }

  SECTION("Truncate demotes block-backed files to inline storage") {
    constexpr size_t kBlocks = 5;
    const size_t block_size = size_t{1} << quota->block_size_log2();

    auto metadata_block = create_metadata_block(/*log2_size=*/8);
    auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
    metadata->size_category = 1;
    metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
    metadata->size_on_disk = static_cast<uint32_t>(kBlocks * block_size);
    metadata->file_size = static_cast<uint32_t>(block_size);

    auto block_numbers = throw_if_error(quota->AllocDataBlocks(kBlocks, BlockType::Single));
    auto entries = metadata_tail_span<DataBlockMetadata>(metadata_block, metadata, block_numbers.size());
    for (size_t i = 0; i < block_numbers.size(); ++i) {
      entries[i].block_number = block_numbers[i];
      std::fill(std::begin(entries[i].hash), std::end(entries[i].hash), uint8_be_t{0});
    }

    Entry::MetadataRef metadata_ref{metadata_block, 0};
    auto file = std::make_shared<File>("demote-to-inline", metadata_ref, quota);

    const auto large_block_size =
        size_t{1} << (quota->block_size_log2() + log2_size(BlockType::Large));
    const size_t new_size = block_size * (kBlocks + 1);
    file->EnsureSize(new_size);
    CHECK(metadata->size_category.value() == 2);
    CHECK(file->SizeOnDisk() == large_block_size);

    File::file_device io(file);
    std::vector<char> pattern(new_size);
    for (size_t i = 0; i < pattern.size(); ++i) {
      pattern[i] = static_cast<char>('a' + (i % 26));
    }
    REQUIRE(io.seek(0, std::ios_base::beg) == 0);
    auto written = io.write(pattern.data(), static_cast<std::streamsize>(pattern.size()));
    CHECK(written == static_cast<std::streamsize>(pattern.size()));

    constexpr size_t kInlineSize = 80;
    file->Truncate(kInlineSize);
    CHECK(metadata->size_category.value() == 0);
    CHECK(file->Size() == kInlineSize);
    CHECK(file->SizeOnDisk() == kInlineSize);

    std::array<char, kInlineSize> verify{};
    REQUIRE(io.seek(0, std::ios_base::beg) == 0);
    auto read = io.read(verify.data(), verify.size());
    CHECK(read == static_cast<std::streamsize>(kInlineSize));
    CHECK(std::equal(verify.begin(), verify.end(), pattern.begin()));
  }
}

TEST_CASE("File storage transitions migrate only when required") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto wfs_device = *WfsDevice::Create(device);
  auto quota = wfs_device->GetRootArea();

  const size_t block_size = size_t{1} << quota->block_size_log2();
  const size_t large_block_size =
      size_t{1} << file_internal::block_capacity_log2(quota->block_size_log2(), BlockType::Large);
  const size_t cluster_size =
      size_t{1} << file_internal::block_capacity_log2(quota->block_size_log2(), BlockType::Cluster);

  auto metadata_block = create_metadata_block(/*log2_size=*/12);
  auto* metadata = metadata_block->get_mutable_object<EntryMetadata>(0);
  metadata->size_category = static_cast<uint8_t>(file_internal::DataType::InMetadata);
  metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
  auto inline_capacity =
      (size_t{1} << metadata->metadata_log2_size.value()) - metadata->size();
  metadata->size_on_disk = static_cast<uint32_t>(inline_capacity);
  metadata->file_size = 0;

  Entry::MetadataRef metadata_ref{metadata_block, 0};
  auto file = std::make_shared<File>("transitions", metadata_ref, quota);
  File::file_device io(file);

  std::vector<char> pattern(1024);
  std::iota(pattern.begin(), pattern.end(), 0);

  auto expect_storage = [&](file_internal::DataType expected) {
    CHECK(static_cast<file_internal::DataType>(metadata->size_category.value()) == expected);
  };

  // Seed with some data that should remain intact through all migrations.
  REQUIRE(io.seek(0, std::ios_base::beg) == 0);
  REQUIRE(io.write(pattern.data(), static_cast<std::streamsize>(pattern.size())) ==
          static_cast<std::streamsize>(pattern.size()));
  CHECK(file->Size() == pattern.size());
  expect_storage(file_internal::DataType::InMetadata);

  SECTION("Inline remains inline until capacity") {
    file->EnsureSize(inline_capacity);
    expect_storage(file_internal::DataType::InMetadata);
    CHECK(file->Size() == inline_capacity);

    file->EnsureSize(inline_capacity + 1);
    expect_storage(file_internal::DataType::SingleBlocks);
    CHECK(file->SizeOnDisk() == block_size);
  }

  SECTION("Single blocks grow to limit before migrating to large blocks") {
    file->EnsureSize(block_size * 5);
    expect_storage(file_internal::DataType::SingleBlocks);
    CHECK(file->SizeOnDisk() == block_size * 5);

    file->EnsureSize(block_size * 5 + 1);
    expect_storage(file_internal::DataType::LargeBlocks);
    CHECK(file->SizeOnDisk() == large_block_size);
  }

  SECTION("Large blocks migrate only when exceeding their capacity budget") {
    file->EnsureSize(large_block_size * 5);
    expect_storage(file_internal::DataType::LargeBlocks);
    CHECK(file->SizeOnDisk() == large_block_size * 5);

    file->EnsureSize(large_block_size * 5 + 1);
    expect_storage(file_internal::DataType::ClusterBlocks);
    CHECK(file->SizeOnDisk() == cluster_size);
  }

  SECTION("Clusters migrate to extended clusters only after exceeding cluster quota") {
    file->EnsureSize(cluster_size * 4);
    expect_storage(file_internal::DataType::ClusterBlocks);
    CHECK(file->SizeOnDisk() == cluster_size * 4);

    file->EnsureSize(cluster_size * 4 + 1);
    expect_storage(file_internal::DataType::ExtendedClusterBlocks);
    auto expected_clusters = (cluster_size * 4 + 1 + cluster_size - 1) / cluster_size;
    CHECK(file->SizeOnDisk() == expected_clusters * cluster_size);
  }

  SECTION("Data survives migrations across all storage types") {
    auto cluster_capacity = cluster_size * 4 - 16;
    file->EnsureSize(cluster_capacity);
    expect_storage(file_internal::DataType::ClusterBlocks);

    file->EnsureSize(cluster_capacity + cluster_size);
    expect_storage(file_internal::DataType::ExtendedClusterBlocks);

    std::vector<char> verify(pattern.size());
    REQUIRE(io.seek(0, std::ios_base::beg) == 0);
    auto read = io.read(verify.data(), static_cast<std::streamsize>(verify.size()));
    CHECK(read == static_cast<std::streamsize>(verify.size()));
    CHECK(std::ranges::equal(verify, pattern));
  }
}
