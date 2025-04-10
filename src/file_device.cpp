/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_device.h"

#include <cassert>
#include <filesystem>
#include <fstream>

FileDevice::FileDevice(const std::filesystem::path& path,
                       uint32_t log2_sector_size,
                       uint32_t sectors_count,
                       bool read_only,
                       bool open_create)
    : log2_sector_size_(log2_sector_size), sectors_count_(sectors_count), read_only_(read_only) {
  std::ios_base::openmode mode = std::ios::binary | std::ios::in;
  if (!read_only)
    mode |= std::ios::out;
  file_.reset(new std::fstream(path, mode));
  if (file_->fail() && open_create) {
    // try to create the file
    mode |= std::ios::trunc;
    file_.reset(new std::fstream(path, mode));
  }
  if (file_->fail()) {
    throw std::runtime_error("FileDevice: Failed to open file");
  }
  if (log2_sector_size < 9) {
    throw std::runtime_error("FileDevice: Invalid sector size (<512)");
  }
  if (sectors_count_ == 0)
    sectors_count_ = 0x10;  // we will find the exact sectors count later with
                            // Wfs::DetectSectorsCount
}

void FileDevice::ReadSectors(const std::span<std::byte>& data, uint32_t sector_address, uint32_t sectors_count) {
  assert(data.size() == (sectors_count << log2_sector_size_));
  if (sector_address >= sectors_count_ || sector_address + sectors_count > sectors_count_) {
    throw std::runtime_error("FileDevice: Read out of file.");
  }
  std::lock_guard<std::mutex> guard(io_lock_);
  file_->seekg(static_cast<std::streampos>(sector_address) << log2_sector_size_);
  file_->read(reinterpret_cast<char*>(data.data()), data.size());
  if (file_->gcount() != static_cast<std::streamsize>(data.size()))
    throw std::runtime_error("FileDevice: Failed to read from file.");
}
void FileDevice::WriteSectors(const std::span<std::byte>& data, uint32_t sector_address, uint32_t sectors_count) {
  assert(data.size() == (sectors_count << log2_sector_size_));
  if (read_only_) {
    throw std::runtime_error("FileDevice: Can't write - read only mode");
  }
  if (sector_address >= sectors_count_ || sector_address + sectors_count > sectors_count_) {
    throw std::runtime_error("FileDevice: Write out of file.");
  }
  if (data.size() < sectors_count << log2_sector_size_) {
    throw std::runtime_error("FileDevice: Not enough data for writing.");
  }
  std::lock_guard<std::mutex> guard(io_lock_);
  file_->seekp(static_cast<std::streampos>(sector_address) << log2_sector_size_);
  file_->write(reinterpret_cast<const char*>(data.data()), data.size());
  if (file_->fail())
    throw std::runtime_error("FileDevice: Failed to write to file.");
}

uint64_t FileDevice::GetFileSize() {
  file_->seekg(0, std::ios::end);
  return file_->tellg();
}
