/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_predicate.hpp>

#include "block.h"
#include "errors.h"
#include "file_layout.h"
#include "structs.h"

namespace {
constexpr uint8_t kFilenameLength = 16;
constexpr uint8_t kBlockSizeLog2 = static_cast<uint8_t>(log2_size(BlockSize::Logical));
constexpr uint8_t kPhysicalBlockSizeLog2 = static_cast<uint8_t>(log2_size(BlockSize::Physical));
constexpr uint32_t kSingleBlockSize = uint32_t{1} << kBlockSizeLog2;
constexpr uint32_t kLargeBlockSize = uint32_t{1} << (kBlockSizeLog2 + log2_size(BlockType::Large));
constexpr uint32_t kClusterSize = uint32_t{1} << (kBlockSizeLog2 + log2_size(BlockType::Cluster));

FileLayout Minimum(uint32_t file_size, uint8_t filename_length = kFilenameLength) {
  return CalculateFileLayout(file_size, filename_length, kBlockSizeLog2, FileLayoutMode::MinimumForGrow);
}

FileLayout Maximum(uint32_t file_size, uint8_t filename_length = kFilenameLength) {
  return CalculateFileLayout(file_size, filename_length, kBlockSizeLog2, FileLayoutMode::MaximumForShrink);
}
}  // namespace

TEST_CASE("File layout category 0 inline threshold depends on filename length") {
  constexpr uint8_t short_name = 1;
  constexpr uint8_t long_name = 64;
  CHECK(FileLayoutBaseMetadataSize(short_name) == offsetof(EntryMetadata, case_bitmap) + 1);
  CHECK(FileLayoutBaseMetadataSize(long_name) == offsetof(EntryMetadata, case_bitmap) + 8);
  CHECK(FileLayoutInlineCapacity(short_name) > FileLayoutInlineCapacity(long_name));

  auto inline_limit = FileLayoutInlineCapacity(long_name);
  auto inline_spec = Minimum(inline_limit, long_name);
  CHECK(inline_spec.size_category == 0);
  CHECK(inline_spec.size_on_disk == inline_limit);

  auto external_spec = Minimum(inline_limit + 1, long_name);
  CHECK(external_spec.size_category == 1);
  CHECK(external_spec.size_on_disk == kSingleBlockSize);
}

TEST_CASE("File layout handles empty files") {
  auto spec = Minimum(0);
  CHECK(spec.size_category == 0);
  CHECK(spec.metadata_log2_size == 6);
  CHECK(spec.file_size == 0);
  CHECK(spec.size_on_disk == 0);
  CHECK(spec.data_units_count == 0);
}

TEST_CASE("File layout minimum category 1 thresholds are single blocks") {
  auto one_block = Minimum(FileLayoutInlineCapacity(kFilenameLength) + 1);
  CHECK(one_block.size_category == 1);
  CHECK(one_block.size_on_disk == kSingleBlockSize);
  CHECK(one_block.data_units_count == 1);

  auto five_blocks = Minimum(5 * kSingleBlockSize);
  CHECK(five_blocks.size_category == 1);
  CHECK(five_blocks.size_on_disk == 5 * kSingleBlockSize);
  CHECK(five_blocks.data_units_count == 5);

  auto next = Minimum(5 * kSingleBlockSize + 1);
  CHECK(next.size_category == 2);
  CHECK(next.size_on_disk == kLargeBlockSize);
  CHECK(next.data_units_count == 1);
}

TEST_CASE("File layout minimum category 2 thresholds are large blocks") {
  auto one_large = Minimum(5 * kSingleBlockSize + 1);
  CHECK(one_large.size_category == 2);
  CHECK(one_large.size_on_disk == kLargeBlockSize);
  CHECK(one_large.data_units_count == 1);

  auto five_large = Minimum(5 * kLargeBlockSize);
  CHECK(five_large.size_category == 2);
  CHECK(five_large.size_on_disk == 5 * kLargeBlockSize);
  CHECK(five_large.data_units_count == 5);

  auto next = Minimum(5 * kLargeBlockSize + 1);
  CHECK(next.size_category == 3);
  CHECK(next.size_on_disk == kClusterSize);
  CHECK(next.data_units_count == 1);
}

TEST_CASE("File layout minimum category 3 thresholds are clusters") {
  auto one_cluster = Minimum(5 * kLargeBlockSize + 1);
  CHECK(one_cluster.size_category == 3);
  CHECK(one_cluster.size_on_disk == kClusterSize);
  CHECK(one_cluster.data_units_count == 1);

  auto four_clusters = Minimum(4 * kClusterSize);
  CHECK(four_clusters.size_category == 3);
  CHECK(four_clusters.size_on_disk == 4 * kClusterSize);
  CHECK(four_clusters.data_units_count == 4);

  auto next = Minimum(4 * kClusterSize + 1);
  CHECK(next.size_category == 4);
  CHECK(next.size_on_disk == 5 * kClusterSize);
  CHECK(next.data_units_count == 5);
}

TEST_CASE("File layout category 4 computes metadata block counts") {
  CHECK(FileLayoutCategory4ClustersPerMetadataBlock(kBlockSizeLog2) == 48);
  CHECK(FileLayoutMaxFileSize(kPhysicalBlockSizeLog2) == 237 * 24 * (uint32_t{1} << 18));
  CHECK(FileLayoutMaxFileSize(kBlockSizeLog2) == 0xFFF80000);

  auto first_category4 = Minimum(4 * kClusterSize + 1);
  CHECK(first_category4.size_category == 4);
  CHECK(first_category4.data_units_count == 5);
  CHECK(FileLayoutCategory4MetadataBlocksCount(first_category4.data_units_count, kBlockSizeLog2) == 1);

  auto second_metadata_block = Minimum(48 * kClusterSize + 1);
  CHECK(second_metadata_block.size_category == 4);
  CHECK(second_metadata_block.data_units_count == 49);
  CHECK(FileLayoutCategory4MetadataBlocksCount(second_metadata_block.data_units_count, kBlockSizeLog2) == 2);
}

TEST_CASE("File layout maximum shrink keeps the largest valid category") {
  CHECK(Maximum(FileLayoutInlineCapacity(kFilenameLength)).size_category == 0);
  CHECK(Maximum(FileLayoutInlineCapacity(kFilenameLength) + 1).size_category == 1);
  CHECK(Maximum(kSingleBlockSize + 1).size_category == 2);
  CHECK(Maximum(kLargeBlockSize + 1).size_category == 3);
  CHECK(Maximum(kClusterSize).size_category == 4);
}

TEST_CASE("File layout rounds metadata log2 sizes") {
  auto empty = Minimum(0);
  CHECK(empty.metadata_log2_size == 6);

  auto category1 = Minimum(5 * kSingleBlockSize);
  CHECK(category1.metadata_log2_size == 8);

  auto category3 = Minimum(4 * kClusterSize);
  CHECK(category3.metadata_log2_size == 10);
}

TEST_CASE("File layout throws when requested size exceeds max file size") {
  auto max_file_size = FileLayoutMaxFileSize(kBlockSizeLog2);
  auto max_spec = Minimum(max_file_size);
  CHECK(max_spec.size_category == 4);
  CHECK(max_spec.file_size == max_file_size);
  CHECK(max_spec.size_on_disk == max_file_size);

  CHECK_THROWS_MATCHES(
      CalculateFileLayout(max_file_size + 1, kFilenameLength, kBlockSizeLog2, FileLayoutMode::MinimumForGrow),
      WfsException, Catch::Matchers::Predicate<WfsException>([](const WfsException& e) {
        return e.error() == WfsError::kFileTooLarge;
      }));
}
