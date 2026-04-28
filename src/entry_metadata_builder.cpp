/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "entry_metadata_builder.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cctype>
#include <cstring>
#include <limits>

#include "file_layout.h"
#include "utils.h"

namespace {
constexpr uint8_t kMinEntryMetadataLog2Size = 6;
[[maybe_unused]] constexpr uint8_t kMaxEntryMetadataLog2Size = 10;

void ApplyNameMetadata(EntryMetadataBuilder::Metadata& metadata, const EntryMetadataBuilder::NormalizedName& name) {
  auto* entry_metadata = metadata.get();
  assert(entry_metadata != nullptr);
  assert(name.key.size() == name.filename_length);
  entry_metadata->filename_length = name.filename_length;

  auto bytes = metadata.mutable_bytes();
  assert(offsetof(EntryMetadata, case_bitmap) + name.case_bitmap.size() <= bytes.size());
  std::memcpy(bytes.data() + offsetof(EntryMetadata, case_bitmap), name.case_bitmap.data(), name.case_bitmap.size());
}

void ApplyCommonMetadata(EntryMetadataBuilder::Metadata& metadata,
                         const EntryMetadataBuilder::NormalizedName& name,
                         const EntryMetadataBuilder::Attributes& attributes) {
  metadata.get()->permissions = attributes.permissions;
  metadata.get()->ctime = attributes.ctime;
  metadata.get()->mtime = attributes.mtime;
  ApplyNameMetadata(metadata, name);
}
}  // namespace

EntryMetadataBuilder::Metadata::Metadata(uint8_t metadata_log2_size)
    : bytes_(size_t{1} << metadata_log2_size, std::byte{0}) {
  assert(metadata_log2_size >= kMinEntryMetadataLog2Size);
  assert(metadata_log2_size <= kMaxEntryMetadataLog2Size);
  get()->metadata_log2_size = metadata_log2_size;
}

EntryMetadata* EntryMetadataBuilder::Metadata::get() {
  return reinterpret_cast<EntryMetadata*>(bytes_.data());
}

const EntryMetadata* EntryMetadataBuilder::Metadata::get() const {
  return reinterpret_cast<const EntryMetadata*>(bytes_.data());
}

size_t EntryMetadataBuilder::Metadata::size() const {
  return bytes_.size();
}

std::span<std::byte> EntryMetadataBuilder::Metadata::mutable_bytes() {
  return bytes_;
}

uint8_t EntryMetadataBuilder::Log2Size(size_t metadata_size) {
  assert(metadata_size != 0);
  const auto log2_size =
      static_cast<uint8_t>(std::max<size_t>(kMinEntryMetadataLog2Size, std::bit_width(metadata_size - 1)));
  assert(log2_size <= kMaxEntryMetadataLog2Size);
  return log2_size;
}

uint8_t EntryMetadataBuilder::BaseLog2Size(uint8_t filename_length) {
  return Log2Size(FileLayout::BaseMetadataSize(filename_length));
}

std::expected<EntryMetadataBuilder::NormalizedName, WfsError> EntryMetadataBuilder::NormalizeName(
    std::string_view name) {
  if (name.empty() || name.size() > std::numeric_limits<uint8_t>::max() || name.find('/') != std::string_view::npos)
    return std::unexpected(WfsError::kInvalidEntryName);

  NormalizedName normalized{
      .key = {},
      .case_bitmap = std::vector<uint8_t>(div_ceil(name.size(), 8), 0),
      .filename_length = static_cast<uint8_t>(name.size()),
  };
  normalized.key.reserve(name.size());

  for (size_t i = 0; i < name.size(); ++i) {
    const auto ch = static_cast<unsigned char>(name[i]);
    const auto lower = static_cast<char>(std::tolower(ch));
    normalized.key.push_back(lower);
    if (static_cast<char>(ch) != lower)
      normalized.case_bitmap[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
  }

  return normalized;
}

EntryMetadataBuilder::Metadata EntryMetadataBuilder::CreateEmptyFile(const NormalizedName& name,
                                                                     const Attributes& attributes,
                                                                     uint8_t block_size_log2) {
  const auto layout = FileLayout::Calculate(0, 0, name.filename_length, block_size_log2, FileLayoutCategory::Inline);
  Metadata metadata(layout.metadata_log2_size);
  auto* entry_metadata = metadata.get();
  entry_metadata->flags = EntryMetadata::UNENCRYPTED_FILE;
  entry_metadata->file_size = layout.file_size;
  entry_metadata->size_on_disk = layout.size_on_disk;
  entry_metadata->size_category = FileLayout::CategoryValue(layout.category);
  ApplyCommonMetadata(metadata, name, attributes);
  return metadata;
}

EntryMetadataBuilder::Metadata EntryMetadataBuilder::CreateDirectory(const NormalizedName& name,
                                                                     const Attributes& attributes,
                                                                     uint32_t directory_block_number) {
  Metadata metadata(BaseLog2Size(name.filename_length));
  auto* entry_metadata = metadata.get();
  entry_metadata->flags = EntryMetadata::DIRECTORY;
  entry_metadata->file_size = 0;
  entry_metadata->size_on_disk = 0;
  entry_metadata->directory_block_number = directory_block_number;
  ApplyCommonMetadata(metadata, name, attributes);
  return metadata;
}
