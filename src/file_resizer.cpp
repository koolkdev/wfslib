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

#include "block.h"
#include "directory_map.h"
#include "errors.h"
#include "file_layout.h"
#include "quota_area.h"

namespace {
FileLayoutCategory CurrentCategory(const EntryMetadata* metadata) {
  return FileLayout::CategoryFromValue(metadata->size_category.value());
}

std::span<const std::byte> InlinePayload(const EntryMetadata* metadata) {
  return {reinterpret_cast<const std::byte*>(metadata) + metadata->size(), metadata->size_on_disk.value()};
}

std::span<std::byte> InlinePayload(EntryMetadata* metadata) {
  return {reinterpret_cast<std::byte*>(metadata) + metadata->size(), metadata->size_on_disk.value()};
}

[[noreturn]] void ThrowResizeUnimplemented() {
  throw std::logic_error("File resize for this layout transition is not implemented");
}
}  // namespace

// TODO: This is a Block only because target data-block Flush writes hashes through block-backed HashRef. If resize
// gets a staged data-block write path, keep replacement metadata as plain owned bytes instead.
EntryMetadataReplacement::EntryMetadataReplacement(const EntryMetadata* source, const FileLayout& layout)
    : block_(Block::CreateDetached(std::vector(size_t{1} << layout.metadata_log2_size, std::byte{0}))) {
  auto* metadata = get();
  std::memcpy(metadata, source, source->size());
  metadata->file_size = layout.file_size;
  metadata->size_on_disk = layout.size_on_disk;
  metadata->metadata_log2_size = layout.metadata_log2_size;
  metadata->size_category = FileLayout::CategoryValue(layout.category);
}

EntryMetadata* EntryMetadataReplacement::get() {
  return block_->get_mutable_object<EntryMetadata>(0);
}

const EntryMetadata* EntryMetadataReplacement::get() const {
  return block_->get_object<EntryMetadata>(0);
}

const std::shared_ptr<Block>& EntryMetadataReplacement::block() const {
  return block_;
}

FileResizer::FileResizer(std::shared_ptr<File> file) : file_(std::move(file)) {}

void FileResizer::Resize(size_t new_size) {
  if (new_size > std::numeric_limits<uint32_t>::max())
    throw WfsException(WfsError::kFileTooLarge);

  const auto* metadata = file_->metadata();
  const auto old_size = metadata->file_size.value();
  const auto target_size = static_cast<uint32_t>(new_size);
  if (target_size == old_size)
    return;

  const auto current_category = CurrentCategory(metadata);
  const auto target_layout = FileLayout::Calculate(old_size, target_size, metadata->filename_length.value(),
                                                   file_->quota()->block_size_log2(), current_category);

  if (target_layout.category != current_category) {
    ResizeAcrossLayouts(target_layout);
    return;
  }

  switch (target_layout.category) {
    case FileLayoutCategory::Inline:
      ResizeInline(target_layout);
      return;
    case FileLayoutCategory::Blocks:
      ResizeDataUnitLayout<FileLayoutCategory::Blocks>(target_layout);
      return;
    case FileLayoutCategory::LargeBlocks:
      ResizeDataUnitLayout<FileLayoutCategory::LargeBlocks>(target_layout);
      return;
    case FileLayoutCategory::Clusters:
      ResizeDataUnitLayout<FileLayoutCategory::Clusters>(target_layout);
      return;
    case FileLayoutCategory::ClusterMetadataBlocks:
      break;
  }

  ThrowResizeUnimplemented();
}

void FileResizer::ResizeInline(const FileLayout& target_layout) {
  const auto* metadata = file_->metadata();
  EntryMetadataReplacement replacement(metadata, target_layout);

  const auto bytes_to_preserve = std::min(metadata->file_size.value(), target_layout.file_size);
  if (bytes_to_preserve != 0)
    std::memcpy(InlinePayload(replacement.get()).data(), InlinePayload(metadata).data(), bytes_to_preserve);

  ReplaceMetadata(replacement.get());
}

void FileResizer::ReplaceMetadata(EntryMetadata* metadata) {
  const auto& directory_map = file_->handle_->directory_map();
  if (!directory_map)
    throw std::logic_error("File metadata is not attached to a directory entry");

  auto result = directory_map->replace_metadata(file_->handle_->key(), metadata);
  if (!result.has_value())
    throw WfsException(result.error());
}
