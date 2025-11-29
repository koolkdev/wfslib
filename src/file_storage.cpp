/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <vector>

#include "block.h"
#include "errors.h"
#include "file_internal.h"
#include "quota_area.h"
#include "structs.h"
#include "utils.h"

using file_internal::block_capacity_log2;
using file_internal::make_subspan;
using file_internal::metadata_array;
using file_internal::metadata_payload_capacity;
using file_internal::mutable_metadata_array;

struct FileDataRef {
  std::shared_ptr<Block> data_block;
  size_t offset_in_block;
  size_t size;
};

class File::FileStorage {
 public:
  explicit FileStorage(std::shared_ptr<File> file) : file_(std::move(file)) {}
  virtual ~FileStorage() = default;

  virtual std::span<const std::byte> GetData(size_t offset, size_t size) = 0;
  virtual std::span<std::byte> GetMutableData(size_t offset, size_t size) = 0;
  virtual void Resize(size_t new_size) = 0;

  size_t Read(std::byte* output, size_t offset, size_t size) {
    auto span = GetData(offset, size);
    if (span.empty())
      return 0;
    std::copy(span.begin(), span.end(), output);
    return span.size();
  }

  size_t Write(const std::byte* input, size_t offset, size_t size) {
    auto span = GetMutableData(offset, size);
    if (span.empty())
      return 0;
    std::copy(input, input + span.size(), span.begin());
    return span.size();
  }

 protected:
  std::shared_ptr<File> file_;
};

class File::InlineStorage : public File::FileStorage {
 public:
  explicit InlineStorage(const std::shared_ptr<File>& file) : FileStorage(file) {}

  std::span<const std::byte> GetData(size_t offset, size_t size) override {
    auto length = static_cast<size_t>(file_->metadata()->size_on_disk.value());
    auto span = metadata_array<std::byte>(file_->metadata(), length, /*align_to_end=*/false);
    return make_subspan(span, offset, size);
  }

  std::span<std::byte> GetMutableData(size_t offset, size_t size) override {
    auto length = static_cast<size_t>(file_->metadata()->size_on_disk.value());
    auto span = mutable_metadata_array<std::byte>(file_->mutable_metadata(), length, /*align_to_end=*/false);
    if (length == 0) {
      auto capacity = metadata_payload_capacity(*file_->metadata());
      span = mutable_metadata_array<std::byte>(file_->mutable_metadata(), capacity, /*align_to_end=*/false);
    }
    return make_subspan(span, offset, size);
  }

  void Resize(size_t new_size) override {
    auto capacity = metadata_payload_capacity(*file_->metadata());
    if (new_size > capacity)
      throw WfsException(WfsError::kNoSpace);
    auto current_size = static_cast<size_t>(file_->metadata()->file_size.value());
    auto span = mutable_metadata_array<std::byte>(file_->mutable_metadata(), capacity, /*align_to_end=*/false);
    if (current_size > span.size())
      throw WfsException(WfsError::kFileMetadataCorrupted);
    if (new_size > span.size())
      throw WfsException(WfsError::kFileMetadataCorrupted);
    if (new_size > current_size) {
      std::fill(span.begin() + current_size, span.begin() + new_size, std::byte{0});
    }
    file_->mutable_metadata()->file_size = static_cast<uint32_t>(new_size);
    file_->mutable_metadata()->size_on_disk = static_cast<uint32_t>(new_size);
  }
};

class File::BlockStorage : public File::FileStorage {
 public:
  explicit BlockStorage(const std::shared_ptr<File>& file) : FileStorage(file) {}

  std::span<const std::byte> GetData(size_t offset, size_t size) override {
    auto ref = GetFileDataRef(offset, size, /*for_writing=*/false);
    return ref.data_block->data().subspan(ref.offset_in_block, ref.size);
  }

  std::span<std::byte> GetMutableData(size_t offset, size_t size) override {
    auto ref = GetFileDataRef(offset, size, /*for_writing=*/true);
    return ref.data_block->mutable_data().subspan(ref.offset_in_block, ref.size);
  }

  void Resize(size_t new_size) override {
    auto current_size = static_cast<size_t>(file_->metadata()->file_size.value());
    if (new_size == current_size)
      return;

    const auto block_log2 = GetDataBlockSize();
    const auto block_mask = (size_t{1} << block_log2) - 1;

    if (new_size < current_size) {
      if (new_size > 0) {
        auto ref = GetFileDataRef(new_size - 1, 1, /*for_writing=*/true);
        auto new_block_size = ((new_size - 1) & block_mask) + 1;
        if (ref.data_block && ref.data_block->size() > new_block_size) {
          ref.data_block->Resize(static_cast<uint32_t>(new_block_size));
        }
      }
      file_->mutable_metadata()->file_size = static_cast<uint32_t>(new_size);
      return;
    }

    while (current_size < new_size) {
      auto ref = GetFileDataRef(current_size, new_size - current_size, /*for_writing=*/true);
      if (!ref.data_block || ref.size == 0)
        throw WfsException(WfsError::kFileDataCorrupted);
      current_size += ref.size;
      file_->mutable_metadata()->file_size = static_cast<uint32_t>(current_size);
    }
  }

 protected:
  virtual BlockType GetDataBlockType() const = 0;

  size_t GetDataBlockSize() const {
    return block_capacity_log2(file_->quota()->block_size_log2(), GetDataBlockType());
  }

  virtual size_t GetMetadataItemsCount() const {
    return div_ceil_pow2(file_->metadata()->size_on_disk.value(), GetDataBlockSize());
  }

  virtual FileDataRef GetFileDataRef(size_t offset, size_t size, bool for_writing) {
    auto count = GetMetadataItemsCount();
    if (count == 0)
      throw WfsException(WfsError::kFileMetadataCorrupted);
    auto blocks_list = metadata_array<DataBlockMetadata>(file_->metadata(), count, /*align_to_end=*/true);
    auto [block_index, offset_in_block] = div_pow2(offset, GetDataBlockSize());
    if (block_index >= blocks_list.size())
      throw WfsException(WfsError::kFileMetadataCorrupted);
    const auto& entry = blocks_list[block_index];
    auto block_offset = floor_pow2(offset, GetDataBlockSize());
    return GetDataFromBlock(entry.block_number.value(), block_offset, offset_in_block, size, file_->metadata_block(),
                            entry.hash, for_writing);
  }

  template <typename ClusterSpan>
  FileDataRef GetFileDataRefFromClustersList(size_t cluster_list_start,
                                             size_t offset,
                                             size_t size,
                                             const std::shared_ptr<Block>& metadata_block,
                                             ClusterSpan clusters_list,
                                             bool for_writing) {
    if (clusters_list.empty())
      throw WfsException(WfsError::kFileMetadataCorrupted);
    auto cluster_log2 = ClusterDataLog2Size();
    auto logical_cluster_index = offset >> cluster_log2;
    if (logical_cluster_index < cluster_list_start)
      throw WfsException(WfsError::kFileMetadataCorrupted);
    auto relative_cluster_index = logical_cluster_index - cluster_list_start;
    if (relative_cluster_index >= clusters_list.size())
      throw WfsException(WfsError::kFileMetadataCorrupted);
    const auto& cluster = clusters_list[relative_cluster_index];
    auto offset_in_cluster = offset - (logical_cluster_index << cluster_log2);
    auto [block_index, offset_in_block] = div_pow2(offset_in_cluster, GetDataBlockSize());
    auto block_offset = floor_pow2(offset, GetDataBlockSize());
    return GetDataFromBlock(cluster.block_number.value() +
                                static_cast<uint32_t>(block_index << log2_size(GetDataBlockType())),
                            block_offset, offset_in_block, size, metadata_block, cluster.hash[block_index],
                            for_writing);
  }

  size_t ClusterDataLog2Size() const {
    return block_capacity_log2(file_->quota()->block_size_log2(), BlockType::Cluster);
  }

 private:
  FileDataRef GetDataFromBlock(uint32_t block_number,
                               size_t block_offset,
                               size_t offset_in_block,
                               size_t requested_size,
                               const std::shared_ptr<Block>& hash_block,
                               const uint8_be_t* hash,
                               bool for_writing) {
    auto block_log2 = GetDataBlockSize();
    auto block_capacity = size_t{1} << block_log2;
    auto wanted_size = std::min(offset_in_block + requested_size, block_capacity);

    size_t current_size = 0;
    auto file_size = static_cast<size_t>(file_->metadata()->file_size.value());
    if (file_size > block_offset) {
      current_size = std::min(block_capacity, static_cast<size_t>(file_size - block_offset));
    }

    LoadDataBlock(block_number, static_cast<uint32_t>(std::max<size_t>(current_size, size_t{1})),
                  {hash_block, hash_block->to_offset(hash)});

    if (!for_writing) {
      current_data_block_->Flush();
      current_data_block_->Fetch(/*check_hash=*/false);
    }

    if (for_writing && current_size < wanted_size) {
      current_data_block_->Resize(static_cast<uint32_t>(wanted_size));
      current_size = wanted_size;
    }

    if (offset_in_block >= current_data_block_->size())
      return {current_data_block_, offset_in_block, 0};

    auto available = current_data_block_->size() - offset_in_block;
    auto span_size = std::min(requested_size, static_cast<size_t>(available));
    return {current_data_block_, offset_in_block, span_size};
  }

  void LoadDataBlock(uint32_t block_number, uint32_t data_size, Block::HashRef data_hash) {
    if (current_data_block_) {
      auto current_block_number =
          file_->quota()->to_area_block_number(current_data_block_->physical_block_number());
      if (current_block_number == block_number)
        return;
    }
    auto block = file_->quota()->LoadDataBlock(
        block_number, static_cast<BlockSize>(file_->quota()->block_size_log2()), GetDataBlockType(), data_size,
        std::move(data_hash), !(file_->metadata()->flags.value() & EntryMetadata::UNENCRYPTED_FILE));
    current_data_block_ = throw_if_error(std::move(block));
  }

  std::shared_ptr<Block> current_data_block_;
};

class File::SingleBlockStorage : public File::BlockStorage {
 public:
  explicit SingleBlockStorage(const std::shared_ptr<File>& file) : BlockStorage(file) {}

 protected:
  BlockType GetDataBlockType() const override { return BlockType::Single; }
};

class File::LargeBlockStorage : public File::BlockStorage {
 public:
  explicit LargeBlockStorage(const std::shared_ptr<File>& file) : BlockStorage(file) {}

 protected:
  BlockType GetDataBlockType() const override { return BlockType::Large; }
};

class File::ClusterBlockStorage : public File::LargeBlockStorage {
 public:
  explicit ClusterBlockStorage(const std::shared_ptr<File>& file) : LargeBlockStorage(file) {}

  size_t GetMetadataItemsCount() const override {
    return div_ceil_pow2(file_->metadata()->size_on_disk.value(), ClusterDataLog2Size());
  }

  std::span<const std::byte> GetData(size_t offset, size_t size) override {
    auto clusters = metadata_array<DataBlocksClusterMetadata>(file_->metadata(), GetMetadataItemsCount(),
                                                              /*align_to_end=*/true);
    auto ref = GetFileDataRefFromClustersList(/*cluster_list_start=*/0, offset, size, file_->metadata_block(),
                                              clusters, /*for_writing=*/false);
    return ref.data_block->data().subspan(ref.offset_in_block, ref.size);
  }

  std::span<std::byte> GetMutableData(size_t offset, size_t size) override {
    auto clusters = metadata_array<DataBlocksClusterMetadata>(file_->metadata(), GetMetadataItemsCount(),
                                                              /*align_to_end=*/true);
    auto ref = GetFileDataRefFromClustersList(/*cluster_list_start=*/0, offset, size, file_->metadata_block(),
                                              clusters, /*for_writing=*/true);
    return ref.data_block->mutable_data().subspan(ref.offset_in_block, ref.size);
  }

  FileDataRef GetFileDataRef(size_t offset, size_t size, bool for_writing) override {
    auto clusters = metadata_array<DataBlocksClusterMetadata>(file_->metadata(), GetMetadataItemsCount(),
                                                              /*align_to_end=*/true);
    return GetFileDataRefFromClustersList(/*cluster_list_start=*/0, offset, size, file_->metadata_block(), clusters,
                                          for_writing);
  }
};

class File::ExtendedClusterBlockStorage : public File::ClusterBlockStorage {
 public:
  explicit ExtendedClusterBlockStorage(const std::shared_ptr<File>& file) : ClusterBlockStorage(file) {}

  size_t GetMetadataItemsCount() const override {
    auto total_clusters = div_ceil_pow2(file_->metadata()->size_on_disk.value(), ClusterDataLog2Size());
    auto clusters_per_block = ClustersInBlock();
    return div_ceil(total_clusters, clusters_per_block);
  }

  std::span<const std::byte> GetData(size_t offset, size_t size) override {
    auto ref = GetFileDataRef(offset, size, /*for_writing=*/false);
    return ref.data_block->data().subspan(ref.offset_in_block, ref.size);
  }

  std::span<std::byte> GetMutableData(size_t offset, size_t size) override {
    auto ref = GetFileDataRef(offset, size, /*for_writing=*/true);
    return ref.data_block->mutable_data().subspan(ref.offset_in_block, ref.size);
  }

  FileDataRef GetFileDataRef(size_t offset, size_t size, bool for_writing) override {
    auto blocks_list =
        metadata_array<uint32_be_t>(file_->metadata(), GetMetadataItemsCount(), /*align_to_end=*/true);
    auto cluster_index = offset >> ClusterDataLog2Size();
    auto block_index = cluster_index / ClustersInBlock();
    if (block_index >= blocks_list.size())
      throw WfsException(WfsError::kFileMetadataCorrupted);

    const auto& entry = blocks_list[block_index];
    LoadMetadataBlock(entry.value());

    auto total_clusters = div_ceil_pow2(file_->metadata()->size_on_disk.value(), ClusterDataLog2Size());
    auto clusters_in_this_block =
        std::min(ClustersInBlock(), total_clusters - block_index * ClustersInBlock());
    auto clusters_span = std::span<const DataBlocksClusterMetadata>{
        current_metadata_block_->get_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader)),
        clusters_in_this_block};

    return GetFileDataRefFromClustersList(block_index * ClustersInBlock(), offset, size, current_metadata_block_,
                                          clusters_span, for_writing);
  }

 private:
  void LoadMetadataBlock(uint32_t block_number) {
    if (current_metadata_block_) {
      auto current = file_->quota()->to_area_block_number(current_metadata_block_->physical_block_number());
      if (current == block_number)
        return;
    }
    current_metadata_block_ = throw_if_error(file_->quota()->LoadMetadataBlock(block_number));
  }

  size_t ClustersInBlock() const {
    auto block_payload = file_->quota()->block_size() - sizeof(MetadataBlockHeader);
    auto clusters = block_payload / sizeof(DataBlocksClusterMetadata);
    return std::min<size_t>(clusters, 48);
  }

  std::shared_ptr<Block> current_metadata_block_;
};

std::shared_ptr<File::FileStorage> File::CreateStorage(std::shared_ptr<File> file) {
  switch (file->metadata()->size_category.value()) {
    case 0:
      return std::make_shared<InlineStorage>(file);
    case 1:
      return std::make_shared<SingleBlockStorage>(file);
    case 2:
      return std::make_shared<LargeBlockStorage>(file);
    case 3:
      return std::make_shared<ClusterBlockStorage>(file);
    case 4:
      return std::make_shared<ExtendedClusterBlockStorage>(file);
    default:
      throw std::runtime_error("Unexpected file category");
  }
}

void File::Resize(size_t new_size) {
  if (new_size > SizeOnDisk())
    throw WfsException(WfsError::kNoSpace);
  if (new_size == Size())
    return;
  auto storage = CreateStorage(shared_from_this());
  storage->Resize(new_size);
}

File::file_device::file_device(const std::shared_ptr<File>& file)
    : file_(file), storage_(CreateStorage(file)), pos_(0),
      storage_type_(static_cast<uint8_t>(file->metadata()->size_category.value())) {}

size_t File::file_device::size() const {
  return file_->Size();
}

std::streamsize File::file_device::read(char_type* s, std::streamsize n) {
  auto file_size = static_cast<std::streamsize>(size());
  auto available = std::max<std::streamsize>(0, file_size - pos_);
  auto to_read = std::min(n, available);
  if (to_read <= 0)
    return -1;

  refresh_storage_if_needed();
  std::streamsize transferred = 0;
  while (to_read > 0) {
    auto chunk = static_cast<std::streamsize>(storage_->Read(
        reinterpret_cast<std::byte*>(s), static_cast<size_t>(pos_), static_cast<size_t>(to_read)));
    if (chunk == 0)
      break;
    s += chunk;
    pos_ += chunk;
    to_read -= chunk;
    transferred += chunk;
  }

  return transferred == 0 ? -1 : transferred;
}

std::streamsize File::file_device::write(const char_type* s, std::streamsize n) {
  if (n <= 0)
    return -1;

  auto current_pos = static_cast<size_t>(pos_);
  auto desired_size = current_pos + static_cast<size_t>(n);
  desired_size = std::min(desired_size, static_cast<size_t>(file_->SizeOnDisk()));

  refresh_storage_if_needed();
  if (desired_size > size())
    storage_->Resize(desired_size);

  auto writable = std::min<std::streamsize>(n, static_cast<std::streamsize>(size() - current_pos));
  if (writable <= 0)
    return -1;

  std::streamsize written = 0;
  while (writable > 0) {
    auto chunk = static_cast<std::streamsize>(storage_->Write(
        reinterpret_cast<const std::byte*>(s), static_cast<size_t>(pos_), static_cast<size_t>(writable)));
    if (chunk == 0)
      break;
    s += chunk;
    pos_ += chunk;
    writable -= chunk;
    written += chunk;
  }

  return written == 0 ? -1 : written;
}

boost::iostreams::stream_offset File::file_device::seek(boost::iostreams::stream_offset off,
                                                        std::ios_base::seekdir way) {
  boost::iostreams::stream_offset next;
  switch (way) {
    case std::ios_base::beg:
      next = off;
      break;
    case std::ios_base::cur:
      next = pos_ + off;
      break;
    case std::ios_base::end:
      next = static_cast<boost::iostreams::stream_offset>(size()) + off;
      break;
    default:
      throw std::ios_base::failure("bad seek direction");
  }

  if (next < 0 || next > static_cast<boost::iostreams::stream_offset>(size()))
    throw std::ios_base::failure("bad seek offset");

  pos_ = next;
  return pos_;
}

std::streamsize File::file_device::optimal_buffer_size() const {
  auto log2 = file_->quota()->block_size_log2() + log2_size(BlockType::Cluster);
  return std::streamsize{1} << log2;
}

void File::file_device::refresh_storage_if_needed() {
  auto category = static_cast<uint8_t>(file_->metadata()->size_category.value());
  if (category == storage_type_)
    return;
  storage_ = CreateStorage(file_);
  storage_type_ = category;
}
