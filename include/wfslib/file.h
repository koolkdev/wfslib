/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/positioning.hpp>
#include <boost/iostreams/stream.hpp>
#include <memory>
#include "entry.h"

class QuotaArea;

class File : public Entry, public std::enable_shared_from_this<File> {
 public:
  class FileStorage;
  class InlineStorage;
  class BlockStorage;
  class SingleBlockStorage;
  class LargeBlockStorage;
  class ClusterBlockStorage;
  class ExtendedClusterBlockStorage;

  File(std::string name, MetadataRef metadata, std::shared_ptr<QuotaArea> quota)
      : Entry(std::move(name), std::move(metadata)), quota_(std::move(quota)) {}

  uint32_t Size() const;
  uint32_t SizeOnDisk() const;
  void Resize(size_t new_size);

  void EnsureSize(size_t size);

  void Truncate(size_t size);

  class file_device {
   public:
    typedef char char_type;
    struct category : public boost::iostreams::seekable_device_tag, public boost::iostreams::optimally_buffered_tag {};
    file_device(const std::shared_ptr<File>& file);

    std::streamsize read(char_type* s, std::streamsize n);
    std::streamsize write(const char_type* s, std::streamsize n);
    boost::iostreams::stream_offset seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way);
    std::streamsize optimal_buffer_size() const;

   private:
    size_t size() const;
    void refresh_storage_if_needed();
    std::shared_ptr<File> file_;
    std::shared_ptr<FileStorage> storage_;
    boost::iostreams::stream_offset pos_;
    uint8_t storage_type_;
  };

  typedef boost::iostreams::stream<file_device> stream;

 private:
  friend struct FileResizer;

  std::shared_ptr<QuotaArea> quota() const { return quota_; }

  // TODO: We may have cyclic reference here if we do cache in area.
  std::shared_ptr<QuotaArea> quota_;

  static std::shared_ptr<FileStorage> CreateStorage(std::shared_ptr<File> file);
};
