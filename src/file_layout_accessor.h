/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "file.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "block.h"
#include "file_layout.h"
#include "quota_area.h"
#include "structs.h"

class File::LayoutAccessor {
  template <typename T, bool AlignToEnd = false>
  auto Metadata() const {
    const auto count = GetMetadataItemsCount();
    const auto file_metadata_size = sizeof(T) * count;
    const auto base_metadata_size = file_->metadata()->size();
    auto* metadata = [&]() {
      if constexpr (std::is_const_v<T>) {
        return reinterpret_cast<const std::byte*>(file_->metadata());
      } else {
        return reinterpret_cast<std::byte*>(file_->mutable_metadata());
      }
    }();
    if constexpr (AlignToEnd) {
      auto* end = metadata + align_to_power_of_2(base_metadata_size + file_metadata_size);
      return std::span<T>{reinterpret_cast<T*>(end - file_metadata_size), count} | std::views::reverse;
    } else {
      return std::span<T>{reinterpret_cast<T*>(metadata + base_metadata_size), count};
    }
  }

 public:
  struct DataRef {
    std::shared_ptr<Block> data_block;
    size_t offset_in_block;
    size_t size;
  };

  struct BlockPosition {
    size_t index;
    size_t offset;
    size_t offset_in_block;
  };

  struct DataBlockRef {
    uint32_t block_number;
    BlockType block_type;
    size_t offset;
    size_t size;
    Block::HashRef hash;
  };

  LayoutAccessor(const std::shared_ptr<File>& file) : file_(file) {}
  virtual ~LayoutAccessor() {}

  virtual size_t GetMetadataSize() const = 0;
  virtual size_t GetMetadataItemsCount() const {
    return FileLayout::MetadataItemsCount(FileLayout::CategoryFromValue(file_->metadata()->size_category.value()),
                                          file_->metadata()->size_on_disk.value(), file_->quota()->block_size_log2());
  }

  virtual std::span<const std::byte> GetData(size_t offset, size_t size) = 0;
  virtual std::span<std::byte> GetMutableData(size_t offset, size_t size) = 0;

  virtual DataRef GetDataRef(size_t offset, size_t size) {
    (void)offset;
    (void)size;
    throw std::runtime_error("Layout does not store data in external blocks");
  }

  virtual std::vector<DataBlockRef> EnumerateBlocks() const { return {}; }

  void CopyTo(LayoutAccessor& destination, size_t bytes) {
    std::vector<std::byte> buffer(std::min(bytes, size_t{1} << file_->quota()->block_size_log2()));
    size_t offset = 0;
    while (offset < bytes) {
      const auto chunk_size = std::min(bytes - offset, buffer.size());
      const auto read = Read(buffer.data(), offset, chunk_size);
      if (read == 0)
        throw std::runtime_error("Failed to copy file layout data");
      const auto wrote = destination.Write(buffer.data(), offset, read);
      if (wrote != read)
        throw std::runtime_error("Failed to write file layout data");
      offset += wrote;
    }
  }

  virtual void ResizeLastBlock(size_t file_size) { (void)file_size; }

  virtual void FreeOwnedBlocks() {
    for (const auto& block : EnumerateBlocks()) {
      if (!file_->quota()->DeleteBlocks(block.block_number, uint32_t{1} << log2_size(block.block_type)))
        throw WfsException(WfsError::kFreeBlocksAllocatorCorrupted);
    }
  }

  size_t Read(std::byte* output, size_t offset, size_t size) {
    auto data = GetData(offset, size);
    std::copy(data.begin(), data.end(), output);
    return data.size();
  }

  size_t Write(const std::byte* input, size_t offset, size_t size) {
    auto data = GetMutableData(offset, size);
    std::copy(input, input + data.size(), data.begin());
    return data.size();
  }

  virtual void Resize(size_t new_size) = 0;

 protected:
  auto InlinePayload() const { return Metadata<const std::byte>(); }
  auto MutableInlinePayload() const { return Metadata<std::byte>(); }
  auto DataBlockRefs() const { return Metadata<const DataBlockMetadata, true>(); }
  auto MutableDataBlockRefs() const { return Metadata<DataBlockMetadata, true>(); }
  auto ClusterRefs() const { return Metadata<const DataBlocksClusterMetadata, true>(); }
  auto MutableClusterRefs() const { return Metadata<DataBlocksClusterMetadata, true>(); }
  auto ClusterMetadataBlockRefs() const { return Metadata<const uint32_be_t, true>(); }
  auto MutableClusterMetadataBlockRefs() const { return Metadata<uint32_be_t, true>(); }

  BlockPosition BlockPositionForOffset(size_t offset, size_t log2_block_size) const {
    auto [index, offset_in_block] = div_pow2(offset, log2_block_size);
    return {index, floor_pow2(offset, log2_block_size), offset_in_block};
  }

  size_t DataBlockLog2Size(BlockType type) const { return file_->quota()->block_size_log2() + log2_size(type); }

  size_t ClusterDataLog2Size() const { return DataBlockLog2Size(BlockType::Cluster); }

  size_t ClustersPerMetadataBlock() const {
    return FileLayout::ClustersPerClusterMetadataBlock(file_->quota()->block_size_log2());
  }

  uint32_t DataSizeForBlock(size_t file_size, size_t block_offset, size_t log2_block_size) const {
    if (file_size <= block_offset)
      return 0;
    return static_cast<uint32_t>(std::min(size_t{1} << log2_block_size, file_size - block_offset));
  }

  Block::HashRef HashRef(const std::shared_ptr<Block>& hash_block, const uint8_be_t* hash) const {
    return {hash_block, hash_block->to_offset(hash)};
  }

  std::shared_ptr<File> file_;
};
