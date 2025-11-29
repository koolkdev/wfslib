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
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "block.h"
#include "errors.h"
#include "file_internal.h"
#include "quota_area.h"
#include "structs.h"
#include "utils.h"

using file_internal::block_capacity_log2;
using file_internal::clusters_per_metadata_block;
using file_internal::metadata_array;
using file_internal::metadata_payload_capacity;
using file_internal::mutable_metadata_array;
using file_internal::DataType;

struct CategoryLayout {
  DataType data_type;
  uint32_t size_on_disk;
  uint32_t direct_blocks;
  uint32_t clusters;
  uint32_t metadata_blocks;
};

class FileResizer {
 public:
  explicit FileResizer(File& file) : file_(file), quota_(file.quota()) {}

  void Resize(uint32_t target_size);

 private:
  CategoryLayout CalculateLayout(uint32_t target_size) const;
  bool MatchesCurrentLayout(const CategoryLayout& layout) const;
  void RebuildLayout(const CategoryLayout& layout, uint32_t target_size);
  std::shared_ptr<File> CloneCurrentFile() const;
  void CopyInlineData(const std::shared_ptr<File>& old_file, uint32_t target_size);
  void PrepareDirectBlocks(const CategoryLayout& layout, BlockType block_type);
  void PrepareClusterBlocks(const CategoryLayout& layout);
  void StreamCopyData(const std::shared_ptr<File>& old_file, uint32_t target_size);
  void FreeLayoutResources(const std::shared_ptr<File>& old_file) const;
  void FreeDirectBlocks(const EntryMetadata& metadata, BlockType block_type) const;
  void FreeClusterBlocks(const EntryMetadata& metadata) const;
  void FreeExtendedClusterBlocks(const EntryMetadata& metadata) const;

  size_t InlineCapacity() const { return metadata_payload_capacity(*file_.metadata()); }
  size_t SingleBlockSize() const { return size_t{1} << quota_->block_size_log2(); }
  size_t LargeBlockSize() const { return size_t{1} << block_capacity_log2(quota_->block_size_log2(), BlockType::Large); }
  size_t ClusterSize() const {
    return size_t{1} << block_capacity_log2(quota_->block_size_log2(), BlockType::Cluster);
  }
  size_t ClustersPerMetadataBlock() const { return clusters_per_metadata_block(*quota_); }
  bool IsEncrypted() const { return !(file_.metadata()->flags.value() & EntryMetadata::UNENCRYPTED_FILE); }

  File& file_;
  std::shared_ptr<QuotaArea> quota_;
  std::vector<std::shared_ptr<Block>> new_metadata_blocks_;
};

void FileResizer::Resize(uint32_t target_size) {
  auto layout = CalculateLayout(target_size);
  if (MatchesCurrentLayout(layout)) {
    file_.Resize(target_size);
    return;
  }
  RebuildLayout(layout, target_size);
}

CategoryLayout FileResizer::CalculateLayout(uint32_t target_size) const {
  CategoryLayout layout{};
  layout.data_type = DataType::InMetadata;
  layout.size_on_disk = target_size;

  constexpr size_t kMaxCategory1Blocks = 5;
  constexpr size_t kMaxCategory2LargeBlocks = 5;
  constexpr size_t kMaxCategory3Clusters = 4;

  if (target_size <= InlineCapacity())
    return layout;

  auto target_size_sz = static_cast<size_t>(target_size);
  auto single_blocks = div_ceil(target_size_sz, SingleBlockSize());
  if (single_blocks <= kMaxCategory1Blocks) {
    layout.data_type = DataType::SingleBlocks;
    layout.direct_blocks = static_cast<uint32_t>(single_blocks);
    layout.size_on_disk = static_cast<uint32_t>(single_blocks * SingleBlockSize());
    return layout;
  }

  auto large_blocks = div_ceil(single_blocks, size_t{1} << log2_size(BlockType::Large));
  if (large_blocks <= kMaxCategory2LargeBlocks) {
    layout.data_type = DataType::LargeBlocks;
    layout.direct_blocks = static_cast<uint32_t>(large_blocks);
    layout.size_on_disk = static_cast<uint32_t>(large_blocks * LargeBlockSize());
    return layout;
  }

  auto clusters = div_ceil(large_blocks, size_t{1} << log2_size(BlockType::Large));
  if (clusters <= kMaxCategory3Clusters) {
    layout.data_type = DataType::ClusterBlocks;
    layout.clusters = static_cast<uint32_t>(clusters);
    layout.size_on_disk = static_cast<uint32_t>(clusters * ClusterSize());
    return layout;
  }

  auto total_clusters = div_ceil(target_size_sz, ClusterSize());
  layout.data_type = DataType::ExtendedClusterBlocks;
  layout.clusters = static_cast<uint32_t>(total_clusters);
  auto metadata_blocks = div_ceil(total_clusters, ClustersPerMetadataBlock());
  layout.metadata_blocks = static_cast<uint32_t>(metadata_blocks);
  layout.size_on_disk =
      total_clusters == 0 ? 0u : static_cast<uint32_t>(total_clusters * ClusterSize());
  return layout;
}

bool FileResizer::MatchesCurrentLayout(const CategoryLayout& layout) const {
  auto current_category = static_cast<DataType>(file_.metadata()->size_category.value());
  return current_category == layout.data_type && file_.SizeOnDisk() == layout.size_on_disk;
}

void FileResizer::RebuildLayout(const CategoryLayout& layout, uint32_t target_size) {
  auto old_file = CloneCurrentFile();
  assert(old_file->Size() == file_.Size());
  new_metadata_blocks_.clear();
  bool flush_metadata_blocks = false;

  file_.mutable_metadata()->size_category = static_cast<uint8_t>(layout.data_type);
  file_.mutable_metadata()->size_on_disk = layout.size_on_disk;
  file_.mutable_metadata()->file_size = 0;

  switch (layout.data_type) {
    case DataType::InMetadata:
      CopyInlineData(old_file, target_size);
      break;
    case DataType::SingleBlocks: {
      PrepareDirectBlocks(layout, BlockType::Single);
      StreamCopyData(old_file, target_size);
      break;
    }
    case DataType::LargeBlocks: {
      PrepareDirectBlocks(layout, BlockType::Large);
      StreamCopyData(old_file, target_size);
      break;
    }
    case DataType::ClusterBlocks:
      PrepareClusterBlocks(layout);
      StreamCopyData(old_file, target_size);
      break;
    case DataType::ExtendedClusterBlocks:
      PrepareClusterBlocks(layout);
      StreamCopyData(old_file, target_size);
      flush_metadata_blocks = true;
      break;
  }

  if (flush_metadata_blocks) {
    for (auto& metadata_block : new_metadata_blocks_)
      metadata_block->Flush();
  }

  file_.mutable_metadata()->size_category = static_cast<uint8_t>(layout.data_type);
  file_.mutable_metadata()->size_on_disk = layout.size_on_disk;
  file_.mutable_metadata()->file_size = target_size;

  FreeLayoutResources(old_file);
}

std::shared_ptr<File> FileResizer::CloneCurrentFile() const {
  auto entry_size = size_t{1} << file_.metadata()->metadata_log2_size.value();
  auto offset = file_.metadata_block()->to_offset(file_.metadata());
  auto source = file_.metadata_block()->data().subspan(offset, entry_size);
  std::vector<std::byte> copy(entry_size);
  std::copy(source.begin(), source.end(), copy.begin());
  auto clone_block = Block::CreateDetached(std::move(copy));
  Entry::MetadataRef clone_ref{clone_block, 0};
  return std::make_shared<File>(std::string(file_.name()), clone_ref, quota_);
}

void FileResizer::CopyInlineData(const std::shared_ptr<File>& old_file, uint32_t target_size) {
  auto capacity = InlineCapacity();
  if (target_size > capacity)
    throw WfsException(WfsError::kNoSpace);
  auto span = mutable_metadata_array<std::byte>(file_.mutable_metadata(), capacity, /*align_to_end=*/false);
  auto bytes_to_copy = std::min<size_t>(target_size, old_file->Size());

  if (old_file->metadata()->size_category.value() == static_cast<uint8_t>(DataType::InMetadata)) {
    auto old_span =
        metadata_array<std::byte>(old_file->metadata(), old_file->metadata()->size_on_disk.value(),
                                  /*align_to_end=*/false);
    bytes_to_copy = std::min(bytes_to_copy, old_span.size());
    std::copy_n(old_span.begin(), bytes_to_copy, span.begin());
  } else if (bytes_to_copy > 0) {
    std::vector<char> buffer(std::min<size_t>(bytes_to_copy, 64 * 1024));
    File::file_device source(old_file);
    source.seek(0, std::ios_base::beg);
    size_t copied = 0;
    while (copied < bytes_to_copy) {
      auto chunk = std::min(buffer.size(), bytes_to_copy - copied);
      auto read = source.read(buffer.data(), static_cast<std::streamsize>(chunk));
      if (read <= 0)
        throw WfsException(WfsError::kFileDataCorrupted);
      std::memcpy(span.data() + copied, buffer.data(), static_cast<size_t>(read));
      copied += static_cast<size_t>(read);
    }
  }

  if (bytes_to_copy < target_size) {
    std::fill(span.begin() + bytes_to_copy, span.begin() + target_size, std::byte{0});
  }
}

void FileResizer::PrepareDirectBlocks(const CategoryLayout& layout, BlockType block_type) {
  if (layout.direct_blocks == 0)
    return;
  auto block_numbers = throw_if_error(quota_->AllocDataBlocks(layout.direct_blocks, block_type));
  auto entries = mutable_metadata_array<DataBlockMetadata>(file_.mutable_metadata(), block_numbers.size(),
                                                           /*align_to_end=*/true);
  for (size_t i = 0; i < block_numbers.size(); ++i) {
    auto& entry = entries[i];
    entry.block_number = block_numbers[i];
    std::fill(std::begin(entry.hash), std::end(entry.hash), uint8_be_t{0});
  }
}

void FileResizer::PrepareClusterBlocks(const CategoryLayout& layout) {
  if (layout.clusters == 0)
    return;
  auto cluster_numbers = throw_if_error(quota_->AllocDataBlocks(layout.clusters, BlockType::Cluster));
  const size_t kBlocksPerCluster = size_t{1} << log2_size(BlockType::Large);

  if (layout.data_type == DataType::ClusterBlocks) {
    auto entries =
        mutable_metadata_array<DataBlocksClusterMetadata>(file_.mutable_metadata(), cluster_numbers.size(),
                                                          /*align_to_end=*/true);
    for (size_t i = 0; i < cluster_numbers.size(); ++i) {
      auto& entry = entries[i];
      entry.block_number = cluster_numbers[i];
      for (size_t block_idx = 0; block_idx < kBlocksPerCluster; ++block_idx) {
        std::fill(std::begin(entry.hash[block_idx]), std::end(entry.hash[block_idx]), uint8_be_t{0});
      }
    }
    return;
  }

  auto metadata_blocks =
      mutable_metadata_array<uint32_be_t>(file_.mutable_metadata(), layout.metadata_blocks, /*align_to_end=*/true);
  new_metadata_blocks_.clear();

  size_t clusters_remaining = cluster_numbers.size();
  size_t cluster_offset = 0;
  for (size_t metadata_index = 0; metadata_index < layout.metadata_blocks; ++metadata_index) {
    auto metadata_block = throw_if_error(quota_->AllocMetadataBlock());
    auto area_block_number = quota_->to_area_block_number(metadata_block->physical_block_number());
    metadata_blocks[metadata_index] = area_block_number;

    auto* header = metadata_block->get_mutable_object<MetadataBlockHeader>(0);
    std::memset(header, 0, sizeof(MetadataBlockHeader));

    auto clusters_in_block = std::min(ClustersPerMetadataBlock(), clusters_remaining);
    auto* base_entry =
        metadata_block->get_mutable_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader));
    std::span<DataBlocksClusterMetadata> cluster_entries{base_entry, clusters_in_block};

    for (size_t local_idx = 0; local_idx < clusters_in_block; ++local_idx) {
      auto& entry = cluster_entries[local_idx];
      auto cluster_global_index = cluster_offset + local_idx;
      entry.block_number = cluster_numbers[cluster_global_index];
      for (size_t block_idx = 0; block_idx < kBlocksPerCluster; ++block_idx) {
        std::fill(std::begin(entry.hash[block_idx]), std::end(entry.hash[block_idx]), uint8_be_t{0});
      }
    }

    new_metadata_blocks_.push_back(metadata_block);
    clusters_remaining -= clusters_in_block;
    cluster_offset += clusters_in_block;
  }
}

void FileResizer::StreamCopyData(const std::shared_ptr<File>& old_file, uint32_t target_size) {
  constexpr size_t kCopyChunk = 64 * 1024;
  std::vector<char> buffer(kCopyChunk, 0);
  File::file_device source(old_file);
  File::file_device sink(file_.shared_from_this());
  source.seek(0, std::ios_base::beg);
  sink.seek(0, std::ios_base::beg);

  auto old_size = old_file->Size();
  size_t copied = 0;
  size_t remaining = target_size;
  while (copied < old_size) {
    auto to_copy = std::min({old_size - copied, buffer.size(), remaining});
    if (to_copy == 0)
      break;
    auto read = source.read(buffer.data(), static_cast<std::streamsize>(to_copy));
    if (read <= 0)
      break;
    auto written = sink.write(buffer.data(), read);
    if (written != read)
      throw WfsException(WfsError::kFileDataCorrupted);
    copied += static_cast<size_t>(read);
    remaining -= static_cast<size_t>(read);
  }

  if (remaining > 0) {
    std::fill(buffer.begin(), buffer.end(), 0);
    while (remaining > 0) {
      auto chunk = std::min(remaining, buffer.size());
      auto written = sink.write(buffer.data(), static_cast<std::streamsize>(chunk));
      if (written <= 0)
        throw WfsException(WfsError::kFileDataCorrupted);
      remaining -= static_cast<size_t>(written);
    }
  }
}

void FileResizer::FreeLayoutResources(const std::shared_ptr<File>& old_file) const {
  const auto* metadata = old_file->metadata();
  switch (static_cast<DataType>(metadata->size_category.value())) {
    case DataType::InMetadata:
      return;
    case DataType::SingleBlocks:
      FreeDirectBlocks(*metadata, BlockType::Single);
      break;
    case DataType::LargeBlocks:
      FreeDirectBlocks(*metadata, BlockType::Large);
      break;
    case DataType::ClusterBlocks:
      FreeClusterBlocks(*metadata);
      break;
    case DataType::ExtendedClusterBlocks:
      FreeExtendedClusterBlocks(*metadata);
      break;
  }
}

void FileResizer::FreeDirectBlocks(const EntryMetadata& metadata, BlockType block_type) const {
  if (metadata.size_on_disk.value() == 0)
    return;
  auto block_log2 = block_capacity_log2(quota_->block_size_log2(), block_type);
  auto count = div_ceil_pow2(metadata.size_on_disk.value(), block_log2);
  if (count == 0)
    return;
  auto entries = metadata_array<DataBlockMetadata>(&metadata, count, /*align_to_end=*/true);
  auto blocks_per_entry = 1u << log2_size(block_type);
  for (const auto& entry : entries) {
    if (!quota_->DeleteBlocks(entry.block_number.value(), blocks_per_entry))
      throw WfsException(WfsError::kFileMetadataCorrupted);
  }
}

void FileResizer::FreeClusterBlocks(const EntryMetadata& metadata) const {
  if (metadata.size_on_disk.value() == 0)
    return;
  auto cluster_log2 = block_capacity_log2(quota_->block_size_log2(), BlockType::Cluster);
  auto clusters = div_ceil_pow2(metadata.size_on_disk.value(), cluster_log2);
  if (clusters == 0)
    return;
  auto entries = metadata_array<DataBlocksClusterMetadata>(&metadata, clusters, /*align_to_end=*/true);
  auto cluster_blocks = 1u << log2_size(BlockType::Cluster);
  for (const auto& entry : entries) {
    if (!quota_->DeleteBlocks(entry.block_number.value(), cluster_blocks))
      throw WfsException(WfsError::kFileMetadataCorrupted);
  }
}

void FileResizer::FreeExtendedClusterBlocks(const EntryMetadata& metadata) const {
  if (metadata.size_on_disk.value() == 0)
    return;
  auto cluster_log2 = block_capacity_log2(quota_->block_size_log2(), BlockType::Cluster);
  auto total_clusters = div_ceil_pow2(metadata.size_on_disk.value(), cluster_log2);
  if (total_clusters == 0)
    return;
  auto metadata_blocks_count = div_ceil(total_clusters, ClustersPerMetadataBlock());
  auto metadata_entries = metadata_array<uint32_be_t>(&metadata, metadata_blocks_count, /*align_to_end=*/true);
  auto cluster_blocks = 1u << log2_size(BlockType::Cluster);
  size_t processed_clusters = 0;
  for (const auto& entry : metadata_entries) {
    auto metadata_block_number = entry.value();
    auto metadata_block = throw_if_error(quota_->LoadMetadataBlock(metadata_block_number));
    auto clusters_in_block = std::min(ClustersPerMetadataBlock(), total_clusters - processed_clusters);
    auto* base_entry =
        metadata_block->get_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader));
    std::span<const DataBlocksClusterMetadata> cluster_entries{base_entry, clusters_in_block};
    for (const auto& cluster : cluster_entries) {
      if (!quota_->DeleteBlocks(cluster.block_number.value(), cluster_blocks))
        throw WfsException(WfsError::kFileMetadataCorrupted);
    }
    processed_clusters += clusters_in_block;
    if (!quota_->DeleteBlocks(metadata_block_number, 1))
      throw WfsException(WfsError::kFileMetadataCorrupted);
  }
}

void File::EnsureSize(size_t size) {
  if (size <= Size())
    return;
  if (size > std::numeric_limits<uint32_t>::max())
    throw WfsException(WfsError::kNoSpace);
  FileResizer(*this).Resize(static_cast<uint32_t>(size));
}

void File::Truncate(size_t size) {
  if (size >= Size())
    return;
  if (size > std::numeric_limits<uint32_t>::max())
    throw WfsException(WfsError::kNoSpace);
  FileResizer(*this).Resize(static_cast<uint32_t>(size));
}

