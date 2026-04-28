/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <string>

#include "block.h"
#include "entry_metadata_builder.h"
#include "file_layout.h"
#include "utils.h"

namespace {
constexpr uint8_t kLogicalBlockSizeLog2 = static_cast<uint8_t>(log2_size(BlockSize::Logical));

EntryMetadataBuilder::Attributes TestAttributes() {
  EntryMetadataBuilder::Attributes attributes;
  attributes.permissions.owner = 0x12345678;
  attributes.permissions.group = 0x23456789;
  attributes.permissions.mode = 0x3456789a;
  attributes.ctime = 0x456789ab;
  attributes.mtime = 0x56789abc;
  return attributes;
}

void CheckTestAttributes(const EntryMetadata* metadata) {
  CHECK(metadata->permissions.owner.value() == 0x12345678);
  CHECK(metadata->permissions.group.value() == 0x23456789);
  CHECK(metadata->permissions.mode.value() == 0x3456789a);
  CHECK(metadata->ctime.value() == 0x456789ab);
  CHECK(metadata->mtime.value() == 0x56789abc);
}
}  // namespace

TEST_CASE("entry names are normalized and preserve caller casing", "[entry-metadata-builder][unit]") {
  auto name = EntryMetadataBuilder::NormalizeName("AbCdEfGhIj");
  REQUIRE(name.has_value());

  CHECK(name->key == "abcdefghij");
  CHECK(name->filename_length == uint8_t{10});
  REQUIRE(name->case_bitmap.size() == 2);
  CHECK(name->case_bitmap[0] == 0x55);
  CHECK(name->case_bitmap[1] == 0x01);

  auto metadata = EntryMetadataBuilder::CreateEmptyFile(*name, TestAttributes(), kLogicalBlockSizeLog2);
  CHECK(metadata.get()->GetCaseSensitiveName(name->key) == "AbCdEfGhIj");
}

TEST_CASE("entry names reject invalid path components", "[entry-metadata-builder][unit]") {
  CHECK(EntryMetadataBuilder::NormalizeName("").error() == WfsError::kInvalidEntryName);
  CHECK(EntryMetadataBuilder::NormalizeName("with/slash").error() == WfsError::kInvalidEntryName);

  const auto too_long_name = std::string(size_t{std::numeric_limits<uint8_t>::max()} + 1, 'a');
  CHECK(EntryMetadataBuilder::NormalizeName(too_long_name).error() == WfsError::kInvalidEntryName);
}

TEST_CASE("entry names accept the maximum stored filename length", "[entry-metadata-builder][unit]") {
  const auto max_name = std::string(std::numeric_limits<uint8_t>::max(), 'A');
  auto name = EntryMetadataBuilder::NormalizeName(max_name);
  REQUIRE(name.has_value());

  CHECK(name->filename_length == std::numeric_limits<uint8_t>::max());
  CHECK(name->key == std::string(std::numeric_limits<uint8_t>::max(), 'a'));
  CHECK(name->case_bitmap.size() == div_ceil(std::numeric_limits<uint8_t>::max(), 8));
}

TEST_CASE("entry metadata log2 size rounds base metadata allocations", "[entry-metadata-builder][unit]") {
  CHECK(EntryMetadataBuilder::BaseLog2Size(uint8_t{1}) == 6);
  CHECK(EntryMetadataBuilder::BaseLog2Size(uint8_t{161}) == 6);
  CHECK(EntryMetadataBuilder::BaseLog2Size(uint8_t{169}) == 7);
}

TEST_CASE("empty file entry metadata uses inline file layout", "[entry-metadata-builder][unit]") {
  auto name = EntryMetadataBuilder::NormalizeName("ReadMe.TXT");
  REQUIRE(name.has_value());

  auto metadata = EntryMetadataBuilder::CreateEmptyFile(*name, TestAttributes(), kLogicalBlockSizeLog2);
  const auto expected_layout =
      FileLayout::Calculate(0, 0, name->filename_length, kLogicalBlockSizeLog2, FileLayoutCategory::Inline);

  CHECK(metadata.size() == (size_t{1} << expected_layout.metadata_log2_size));
  CHECK(metadata.get()->is_file());
  CHECK(!metadata.get()->is_directory());
  CHECK((metadata.get()->flags.value() & EntryMetadata::UNENCRYPTED_FILE) != 0);
  CHECK(metadata.get()->file_size.value() == 0);
  CHECK(metadata.get()->size_on_disk.value() == 0);
  CHECK(metadata.get()->metadata_log2_size.value() == expected_layout.metadata_log2_size);
  CHECK(metadata.get()->size_category.value() == FileLayout::CategoryValue(FileLayoutCategory::Inline));
  CheckTestAttributes(metadata.get());
  CHECK(metadata.get()->filename_length.value() == name->filename_length);
  CHECK(metadata.get()->GetCaseSensitiveName(name->key) == "ReadMe.TXT");
}

TEST_CASE("directory entry metadata stores directory target block", "[entry-metadata-builder][unit]") {
  auto name = EntryMetadataBuilder::NormalizeName("ChildDir");
  REQUIRE(name.has_value());

  auto metadata = EntryMetadataBuilder::CreateDirectory(*name, TestAttributes(), 0x12345);

  CHECK(metadata.size() == (size_t{1} << EntryMetadataBuilder::BaseLog2Size(name->filename_length)));
  CHECK(metadata.get()->is_directory());
  CHECK(!metadata.get()->is_quota());
  CHECK(metadata.get()->flags.value() == EntryMetadata::DIRECTORY);
  CHECK(metadata.get()->file_size.value() == 0);
  CHECK(metadata.get()->size_on_disk.value() == 0);
  CHECK(metadata.get()->directory_block_number.value() == 0x12345);
  CHECK(metadata.get()->metadata_log2_size.value() == EntryMetadataBuilder::BaseLog2Size(name->filename_length));
  CheckTestAttributes(metadata.get());
  CHECK(metadata.get()->filename_length.value() == name->filename_length);
  CHECK(metadata.get()->GetCaseSensitiveName(name->key) == "ChildDir");
}
