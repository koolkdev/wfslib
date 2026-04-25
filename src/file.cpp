/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

#include "block.h"
#include "errors.h"
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

uint32_t File::Size() const {
  return metadata()->file_size.value();
}

uint32_t File::SizeOnDisk() const {
  return metadata()->size_on_disk.value();
}

bool File::IsEncrypted() const {
  return !(metadata()->flags.value() & EntryMetadata::UNENCRYPTED_FILE);
}

void File::Resize(size_t new_size) {
  size_t old_size = metadata()->file_size.value();
  if (new_size == old_size)
    return;

  const auto current_category = FileLayout::CategoryFromValue(metadata()->size_category.value());
  if (current_category != FileLayoutCategory::Inline) {
    // External block allocation is introduced in later resize stages. Preserve the old logical-only behavior for now.
    new_size = std::min(new_size, static_cast<size_t>(metadata()->size_on_disk.value()));
    if (new_size != old_size)
      CreateLayoutAccessor(shared_from_this())->Resize(new_size);
    return;
  }

  const auto file_size = CheckedFileSize(new_size, quota()->block_size_log2());
  const auto layout = FileLayout::Calculate(file_size, metadata()->filename_length.value(), quota()->block_size_log2(),
                                            FileLayoutMode::MinimumForGrow);
  if (layout.category != FileLayoutCategory::Inline)
    throw WfsException(WfsError::kUnsupportedFileResize);

  ResizeInline(layout);
}

void File::ReplaceMetadata(const EntryMetadata* metadata) {
  if (metadata_updater_) {
    metadata_ = throw_if_error(metadata_updater_(metadata));
    return;
  }

  assert(metadata->metadata_log2_size.value() == metadata_->metadata_log2_size.value());
  if (metadata != metadata_.get())
    std::memcpy(metadata_.get_mutable(), metadata, size_t{1} << metadata->metadata_log2_size.value());
}

void File::ResizeInline(const FileLayout& layout) {
  assert(layout.category == FileLayoutCategory::Inline);

  const auto old_size = metadata()->file_size.value();
  const auto bytes_to_preserve = std::min(old_size, layout.file_size);
  std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
  auto* updated_metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());

  std::memcpy(updated_metadata, metadata(), metadata()->size());
  updated_metadata->file_size = layout.file_size;
  updated_metadata->size_on_disk = layout.size_on_disk;
  updated_metadata->metadata_log2_size = layout.metadata_log2_size;
  updated_metadata->size_category = FileLayout::CategoryValue(layout.category);

  std::copy_n(reinterpret_cast<const std::byte*>(metadata()) + metadata()->size(), bytes_to_preserve,
              reinterpret_cast<std::byte*>(updated_metadata) + updated_metadata->size());
  ReplaceMetadata(updated_metadata);
}

File::file_device::file_device(const std::shared_ptr<File>& file)
    : file_(file), layout_(CreateLayoutAccessor(file)), pos_(0) {}

size_t File::file_device::size() const {
  return file_->metadata()->file_size.value();
}

std::streamsize File::file_device::read(char_type* s, std::streamsize n) {
  std::streamsize amt = static_cast<std::streamsize>(size() - pos_);
  std::streamsize result = std::min(n, amt);

  if (result <= 0)
    return -1;  // EOF

  std::streamsize to_read = result;
  while (to_read > 0) {
    size_t read =
        layout_->Read(reinterpret_cast<std::byte*>(s), static_cast<size_t>(pos_), static_cast<size_t>(to_read));
    s += read;
    pos_ += read;
    to_read -= read;
  }
  return result;
}
std::streamsize File::file_device::write(const char_type* s, std::streamsize n) {
  std::streamsize amt = static_cast<std::streamsize>(size() - pos_);
  if (n > amt) {
    // Try to resize file
    // TODO: This call can't stay like that when we will need to allocate new pages and even change the category
    layout_->Resize(std::min(static_cast<size_t>(file_->SizeOnDisk()), static_cast<size_t>(pos_ + n)));
    amt = static_cast<std::streamsize>(size() - pos_);
  }
  std::streamsize result = std::min(n, amt);

  if (result <= 0)
    return -1;  // Failed to resize file

  std::streamsize to_write = result;
  while (to_write > 0) {
    size_t wrote =
        layout_->Write(reinterpret_cast<const std::byte*>(s), static_cast<size_t>(pos_), static_cast<size_t>(to_write));
    s += wrote;
    pos_ += wrote;
    to_write -= wrote;
  }
  return result;
}
boost::iostreams::stream_offset File::file_device::seek(boost::iostreams::stream_offset off,
                                                        std::ios_base::seekdir way) {
  // Determine new value of pos_
  boost::iostreams::stream_offset next;
  if (way == std::ios_base::beg) {
    next = off;
  } else if (way == std::ios_base::cur) {
    next = pos_ + off;
  } else if (way == std::ios_base::end) {
    next = size() + off - 1;
  } else {
    throw std::ios_base::failure("bad seek direction");
  }

  // Check for errors
  if (next < 0 || next >= static_cast<boost::iostreams::stream_offset>(size()))
    throw std::ios_base::failure("bad seek offset");

  pos_ = next;
  return pos_;
}

std::streamsize File::file_device::optimal_buffer_size() const {
  // Max block size. TODO: By category
  // TODO: The pback_buffer_size, which is actually used, is 0x10004, fix it
  return std::streamsize{1} << (log2_size(BlockSize::Logical) + log2_size(BlockType::Cluster));
}
