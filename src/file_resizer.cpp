/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_resizer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "directory_map.h"
#include "errors.h"
#include "file_layout.h"
#include "quota_area.h"

namespace {
FileLayoutCategory Category(const EntryMetadata* metadata) {
  return FileLayout::CategoryFromValue(metadata->size_category.value());
}

std::span<const std::byte> InlinePayload(const EntryMetadata* metadata) {
  return {reinterpret_cast<const std::byte*>(metadata) + metadata->size(), metadata->size_on_disk.value()};
}

std::span<std::byte> InlinePayload(EntryMetadata* metadata) {
  return {reinterpret_cast<std::byte*>(metadata) + metadata->size(), metadata->size_on_disk.value()};
}

[[noreturn]] void ThrowNonInlineResizeUnimplemented() {
  throw std::logic_error("File resize for non-inline layouts is not implemented");
}
}  // namespace

FileResizer::FileResizer(std::shared_ptr<File> file) : file_(std::move(file)) {}

void FileResizer::Resize(size_t new_size) {
  if (new_size > std::numeric_limits<uint32_t>::max())
    throw WfsException(WfsError::kFileTooLarge);

  const auto* metadata = file_->metadata();
  const auto old_size = metadata->file_size.value();
  const auto target_size = static_cast<uint32_t>(new_size);
  if (target_size == old_size)
    return;

  const auto mode = target_size > old_size ? FileLayoutMode::MinimumForGrow : FileLayoutMode::MaximumForShrink;
  const auto target_layout =
      FileLayout::Calculate(target_size, metadata->filename_length.value(), file_->quota()->block_size_log2(), mode);

  if (Category(metadata) != FileLayoutCategory::Inline || target_layout.category != FileLayoutCategory::Inline)
    ThrowNonInlineResizeUnimplemented();

  ResizeInline(target_layout);
}

void FileResizer::ResizeInline(const FileLayout& target_layout) {
  const auto* metadata = file_->metadata();
  const auto allocation_size = size_t{1} << target_layout.metadata_log2_size;
  std::vector<std::byte> replacement_storage(allocation_size, std::byte{0});
  auto* replacement_metadata = reinterpret_cast<EntryMetadata*>(replacement_storage.data());

  std::memcpy(replacement_metadata, metadata, metadata->size());
  replacement_metadata->file_size = target_layout.file_size;
  replacement_metadata->size_on_disk = target_layout.size_on_disk;
  replacement_metadata->metadata_log2_size = target_layout.metadata_log2_size;
  replacement_metadata->size_category = FileLayout::CategoryValue(target_layout.category);

  const auto bytes_to_preserve = std::min(metadata->file_size.value(), target_layout.file_size);
  if (bytes_to_preserve != 0)
    std::memcpy(InlinePayload(replacement_metadata).data(), InlinePayload(metadata).data(), bytes_to_preserve);

  const auto& directory_map = file_->handle_->directory_map();
  if (!directory_map)
    throw std::logic_error("File metadata is not attached to a directory entry");

  auto result = directory_map->replace_metadata(file_->handle_->key(), replacement_metadata);
  if (!result.has_value())
    throw WfsException(result.error());
}
