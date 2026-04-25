/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_resizer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "errors.h"
#include "file.h"
#include "file_layout.h"
#include "file_layout_accessor.h"

namespace {
uint32_t CheckedFileSize(size_t size, uint8_t block_size_log2) {
  if (size > FileLayout::MaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);
  assert(size <= std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(size);
}

}  // namespace

void FileResizer::ResizeExternalWithinCurrentAllocation(File& file, size_t new_size) {
  const auto old_size = file.metadata()->file_size.value();
  new_size = std::min(new_size, static_cast<size_t>(file.metadata()->size_on_disk.value()));
  if (new_size != old_size)
    File::CreateLayoutAccessor(file.shared_from_this())->Resize(new_size);
}

void FileResizer::ResizeInline(File& file, const FileLayout& layout) {
  assert(layout.category == FileLayoutCategory::Inline);

  const auto old_size = file.metadata()->file_size.value();
  const auto bytes_to_preserve = std::min(old_size, layout.file_size);

  std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
  auto* updated_metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());

  std::memcpy(updated_metadata, file.metadata(), file.metadata()->size());
  updated_metadata->file_size = layout.file_size;
  updated_metadata->size_on_disk = layout.size_on_disk;
  updated_metadata->metadata_log2_size = layout.metadata_log2_size;
  updated_metadata->size_category = FileLayout::CategoryValue(layout.category);

  std::copy_n(reinterpret_cast<const std::byte*>(file.metadata()) + file.metadata()->size(), bytes_to_preserve,
              reinterpret_cast<std::byte*>(updated_metadata) + updated_metadata->size());
  if (updated_metadata->metadata_log2_size.value() == file.metadata()->metadata_log2_size.value())
    file.OverwriteMetadata(updated_metadata);
  else
    file.ReallocateMetadata(updated_metadata);
}

void FileResizer::Resize(File& file, size_t new_size) {
  const auto old_size = file.metadata()->file_size.value();
  if (new_size == old_size)
    return;

  const auto current_category = FileLayout::CategoryFromValue(file.metadata()->size_category.value());
  if (current_category != FileLayoutCategory::Inline) {
    ResizeExternalWithinCurrentAllocation(file, new_size);
    return;
  }

  const auto file_size = CheckedFileSize(new_size, file.quota()->block_size_log2());
  const auto layout = FileLayout::Calculate(file_size, file.metadata()->filename_length.value(),
                                            file.quota()->block_size_log2(), FileLayoutMode::MinimumForGrow);
  if (layout.category != FileLayoutCategory::Inline)
    throw std::logic_error("File resize across layout categories is not implemented");

  ResizeInline(file, layout);
}
