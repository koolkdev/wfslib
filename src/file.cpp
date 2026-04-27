/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file.h"

#include <algorithm>
#include <limits>

#include "block.h"
#include "file_layout_accessor.h"
#include "file_resizer.h"

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
  FileResizer(shared_from_this()).Resize(new_size);
}

File::file_device::file_device(const std::shared_ptr<File>& file) : file_(file), pos_(0) {}

size_t File::file_device::size() const {
  return file_->metadata()->file_size.value();
}

std::streamsize File::file_device::read(char_type* s, std::streamsize n) {
  const auto file_size = static_cast<boost::iostreams::stream_offset>(size());
  if (pos_ < 0 || pos_ >= file_size)
    return -1;  // EOF

  std::streamsize amt = static_cast<std::streamsize>(file_size - pos_);
  std::streamsize result = std::min(n, amt);

  if (result <= 0)
    return -1;  // EOF

  auto layout = CreateLayoutAccessor(file_);
  std::streamsize to_read = result;
  while (to_read > 0) {
    size_t read =
        layout->Read(reinterpret_cast<std::byte*>(s), static_cast<size_t>(pos_), static_cast<size_t>(to_read));
    s += read;
    pos_ += read;
    to_read -= read;
  }
  return result;
}
std::streamsize File::file_device::write(const char_type* s, std::streamsize n) {
  if (n <= 0)
    return -1;  // Failed to write

  const auto file_size = static_cast<boost::iostreams::stream_offset>(size());
  if (pos_ < 0 || pos_ > file_size)
    return -1;  // Seek beyond EOF is not supported.

  if (n > std::numeric_limits<boost::iostreams::stream_offset>::max() - pos_)
    throw std::ios_base::failure("bad write size");

  const auto write_end = pos_ + static_cast<boost::iostreams::stream_offset>(n);
  if (write_end > file_size)
    file_->Resize(static_cast<size_t>(write_end));

  const auto resized_file_size = static_cast<boost::iostreams::stream_offset>(size());
  std::streamsize amt = static_cast<std::streamsize>(resized_file_size - pos_);
  std::streamsize result = std::min(n, amt);

  if (result <= 0)
    return -1;  // Failed to resize file

  // Resize can change the layout category, so choose the accessor after metadata is updated.
  auto layout = CreateLayoutAccessor(file_);
  std::streamsize to_write = result;
  while (to_write > 0) {
    size_t wrote =
        layout->Write(reinterpret_cast<const std::byte*>(s), static_cast<size_t>(pos_), static_cast<size_t>(to_write));
    s += wrote;
    pos_ += wrote;
    to_write -= wrote;
  }
  return result;
}
boost::iostreams::stream_offset File::file_device::seek(boost::iostreams::stream_offset off,
                                                        std::ios_base::seekdir way) {
  const auto file_size = static_cast<boost::iostreams::stream_offset>(size());

  // Determine new value of pos_
  boost::iostreams::stream_offset next;
  if (way == std::ios_base::beg) {
    next = off;
  } else if (way == std::ios_base::cur) {
    next = pos_ + off;
  } else if (way == std::ios_base::end) {
    next = file_size + off;
  } else {
    throw std::ios_base::failure("bad seek direction");
  }

  // Check for errors
  if (next < 0 || next > file_size)
    throw std::ios_base::failure("bad seek offset");

  pos_ = next;
  return pos_;
}

std::streamsize File::file_device::optimal_buffer_size() const {
  // Max block size. TODO: By category
  // TODO: The pback_buffer_size, which is actually used, is 0x10004, fix it
  return std::streamsize{1} << (log2_size(BlockSize::Logical) + log2_size(BlockType::Cluster));
}
