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
#include <span>
#include <vector>

#include "block.h"
#include "errors.h"
#include "file_layout.h"
#include "file_layout_accessor.h"

namespace {
struct ExternalDataBlockRef {
  uint32_t block_number;
  BlockType block_type;
  size_t offset;
  uint32_t size;
  Block::HashRef hash;
};

uint32_t CheckedFileSize(size_t size, uint8_t block_size_log2) {
  if (size > FileLayout::MaxFileSize(block_size_log2))
    throw WfsException(WfsError::kFileTooLarge);
  assert(size <= std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(size);
}

FileLayout CurrentLayout(const EntryMetadata* metadata, uint8_t block_size_log2) {
  const auto category = FileLayout::CategoryFromValue(metadata->size_category.value());
  return {
      .category = category,
      .metadata_log2_size = metadata->metadata_log2_size.value(),
      .file_size = metadata->file_size.value(),
      .size_on_disk = metadata->size_on_disk.value(),
      .data_units_count = FileLayout::DataUnitsCount(category, metadata->size_on_disk.value(), block_size_log2),
  };
}

BlockType AllocationBlockType(FileLayoutCategory category) {
  switch (category) {
    case FileLayoutCategory::Blocks:
      return BlockType::Single;
    case FileLayoutCategory::LargeBlocks:
      return BlockType::Large;
    case FileLayoutCategory::Clusters:
      return BlockType::Cluster;
    case FileLayoutCategory::Inline:
    case FileLayoutCategory::ClusterMetadataBlocks:
      break;
  }
  throw std::invalid_argument("Unexpected external file layout category");
}

BlockType DataBlockType(FileLayoutCategory category) {
  switch (category) {
    case FileLayoutCategory::Blocks:
      return BlockType::Single;
    case FileLayoutCategory::LargeBlocks:
    case FileLayoutCategory::Clusters:
      return BlockType::Large;
    case FileLayoutCategory::Inline:
    case FileLayoutCategory::ClusterMetadataBlocks:
      break;
  }
  throw std::invalid_argument("Unexpected external file layout category");
}

size_t DataBlockLog2Size(FileLayoutCategory category, uint8_t block_size_log2) {
  return static_cast<size_t>(block_size_log2) + static_cast<size_t>(log2_size(DataBlockType(category)));
}

uint32_t AllocationBlocksCount(FileLayoutCategory category) {
  return uint32_t{1} << log2_size(AllocationBlockType(category));
}

uint32_t DataSizeForBlock(uint32_t file_size, size_t block_offset, size_t log2_block_size) {
  if (file_size <= block_offset)
    return 0;
  return static_cast<uint32_t>(std::min(size_t{1} << log2_block_size, static_cast<size_t>(file_size) - block_offset));
}

template <typename T>
std::span<T> AlignedMetadataItems(EntryMetadata* metadata, size_t count) {
  auto* metadata_bytes = reinterpret_cast<std::byte*>(metadata);
  const auto metadata_size = sizeof(T) * count;
  auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
  return {reinterpret_cast<T*>(end - metadata_size), count};
}

template <typename T>
std::span<const T> AlignedMetadataItems(const EntryMetadata* metadata, size_t count) {
  const auto* metadata_bytes = reinterpret_cast<const std::byte*>(metadata);
  const auto metadata_size = sizeof(T) * count;
  const auto* end = metadata_bytes + align_to_power_of_2(metadata->size() + metadata_size);
  return {reinterpret_cast<const T*>(end - metadata_size), count};
}

template <typename T>
T& LogicalMetadataItem(std::span<T> items, size_t index) {
  return items[items.size() - index - 1];
}

template <typename T>
const T& LogicalMetadataItem(std::span<const T> items, size_t index) {
  return items[items.size() - index - 1];
}

ExternalDataBlockRef GetExternalDataBlockRef(FileLayoutCategory category,
                                             const EntryMetadata* metadata,
                                             const std::shared_ptr<Block>& metadata_block,
                                             uint8_t block_size_log2,
                                             size_t block_offset,
                                             uint32_t file_size) {
  const auto data_block_type = DataBlockType(category);
  const auto data_block_log2_size = DataBlockLog2Size(category, block_size_log2);
  const auto data_block_size = DataSizeForBlock(file_size, block_offset, data_block_log2_size);

  if (category == FileLayoutCategory::Blocks || category == FileLayoutCategory::LargeBlocks) {
    const auto items_count = FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), block_size_log2);
    const auto items = AlignedMetadataItems<DataBlockMetadata>(metadata, items_count);
    const auto& item = LogicalMetadataItem(items, block_offset >> data_block_log2_size);
    return {item.block_number.value(),
            data_block_type,
            block_offset,
            data_block_size,
            {metadata_block, metadata_block->to_offset(item.hash)}};
  }

  assert(category == FileLayoutCategory::Clusters);
  const auto cluster_log2_size =
      static_cast<size_t>(block_size_log2) + static_cast<size_t>(log2_size(BlockType::Cluster));
  const auto items_count = FileLayout::MetadataItemsCount(category, metadata->size_on_disk.value(), block_size_log2);
  const auto items = AlignedMetadataItems<DataBlocksClusterMetadata>(metadata, items_count);
  const auto cluster_index = block_offset >> cluster_log2_size;
  const auto block_index_in_cluster = (block_offset - (cluster_index << cluster_log2_size)) >> data_block_log2_size;
  const auto& item = LogicalMetadataItem(items, cluster_index);
  return {item.block_number.value() + static_cast<uint32_t>(block_index_in_cluster << log2_size(BlockType::Large)),
          data_block_type,
          block_offset,
          data_block_size,
          {metadata_block, metadata_block->to_offset(item.hash[block_index_in_cluster])}};
}

void FreeAllocatedDataBlocks(const std::shared_ptr<QuotaArea>& quota,
                             FileLayoutCategory category,
                             std::span<const uint32_t> block_numbers) {
  for (const auto block_number : block_numbers) {
    if (!quota->DeleteBlocks(block_number, AllocationBlocksCount(category)))
      throw WfsException(WfsError::kFreeBlocksAllocatorCorrupted);
  }
}

void CopyExternalMetadataItems(const EntryMetadata* old_metadata,
                               EntryMetadata* new_metadata,
                               const FileLayout& old_layout,
                               const FileLayout& new_layout,
                               std::span<const uint32_t> allocated_blocks) {
  const auto preserved_count = std::min(old_layout.data_units_count, new_layout.data_units_count);

  if (new_layout.category == FileLayoutCategory::Blocks || new_layout.category == FileLayoutCategory::LargeBlocks) {
    const auto old_items = AlignedMetadataItems<DataBlockMetadata>(old_metadata, old_layout.data_units_count);
    auto new_items = AlignedMetadataItems<DataBlockMetadata>(new_metadata, new_layout.data_units_count);
    for (size_t i = 0; i < preserved_count; ++i)
      LogicalMetadataItem(new_items, i) = LogicalMetadataItem(old_items, i);
    for (size_t i = 0; i < allocated_blocks.size(); ++i)
      LogicalMetadataItem(new_items, preserved_count + i).block_number = allocated_blocks[i];
    return;
  }

  assert(new_layout.category == FileLayoutCategory::Clusters);
  const auto old_items = AlignedMetadataItems<DataBlocksClusterMetadata>(old_metadata, old_layout.data_units_count);
  auto new_items = AlignedMetadataItems<DataBlocksClusterMetadata>(new_metadata, new_layout.data_units_count);
  for (size_t i = 0; i < preserved_count; ++i)
    LogicalMetadataItem(new_items, i) = LogicalMetadataItem(old_items, i);
  for (size_t i = 0; i < allocated_blocks.size(); ++i)
    LogicalMetadataItem(new_items, preserved_count + i).block_number = allocated_blocks[i];
}

std::vector<uint32_t> RemovedAllocationBlocks(const EntryMetadata* old_metadata,
                                              const FileLayout& old_layout,
                                              const FileLayout& new_layout) {
  std::vector<uint32_t> removed_blocks;
  if (new_layout.data_units_count >= old_layout.data_units_count)
    return removed_blocks;

  removed_blocks.reserve(old_layout.data_units_count - new_layout.data_units_count);
  if (old_layout.category == FileLayoutCategory::Blocks || old_layout.category == FileLayoutCategory::LargeBlocks) {
    const auto old_items = AlignedMetadataItems<DataBlockMetadata>(old_metadata, old_layout.data_units_count);
    for (size_t i = new_layout.data_units_count; i < old_layout.data_units_count; ++i)
      removed_blocks.push_back(LogicalMetadataItem(old_items, i).block_number.value());
    return removed_blocks;
  }

  assert(old_layout.category == FileLayoutCategory::Clusters);
  const auto old_items = AlignedMetadataItems<DataBlocksClusterMetadata>(old_metadata, old_layout.data_units_count);
  for (size_t i = new_layout.data_units_count; i < old_layout.data_units_count; ++i)
    removed_blocks.push_back(LogicalMetadataItem(old_items, i).block_number.value());
  return removed_blocks;
}

void DetachAllocatedDataBlocks(const std::shared_ptr<QuotaArea>& quota,
                               FileLayoutCategory category,
                               std::span<const uint32_t> block_numbers) {
  const auto data_block_type = DataBlockType(category);
  const auto data_blocks_count = AllocationBlocksCount(category) >> log2_size(data_block_type);
  const auto data_block_step = uint32_t{1} << log2_size(data_block_type);
  const auto data_block_size =
      static_cast<uint32_t>(quota->block_size() << static_cast<size_t>(log2_size(data_block_type)));
  for (const auto block_number : block_numbers) {
    for (uint32_t i = 0; i < data_blocks_count; ++i) {
      auto block = throw_if_error(
          quota->LoadDataBlock(block_number + i * data_block_step, static_cast<BlockSize>(quota->block_size_log2()),
                               data_block_type, data_block_size, {}, /*encrypted=*/false, /*new_block=*/true));
      block->Detach();
    }
  }
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
  if (current_category == FileLayoutCategory::ClusterMetadataBlocks) {
    // Category 4 allocation is introduced in a later resize stage. Preserve the old logical-only behavior for now.
    new_size = std::min(new_size, static_cast<size_t>(metadata()->size_on_disk.value()));
    if (new_size != old_size)
      CreateLayoutAccessor(shared_from_this())->Resize(new_size);
    return;
  }

  const auto file_size = CheckedFileSize(new_size, quota()->block_size_log2());
  if (current_category != FileLayoutCategory::Inline) {
    if (!FileLayout::CategoryCanStore(current_category, file_size, metadata()->filename_length.value(),
                                      quota()->block_size_log2()))
      throw WfsException(WfsError::kUnsupportedFileResize);
    ResizeExternal(FileLayout::CalculateForCategory(file_size, metadata()->filename_length.value(),
                                                    quota()->block_size_log2(), current_category));
    return;
  }

  const auto layout = FileLayout::Calculate(file_size, metadata()->filename_length.value(), quota()->block_size_log2(),
                                            FileLayoutMode::MinimumForGrow);
  if (layout.category != FileLayoutCategory::Inline)
    throw WfsException(WfsError::kUnsupportedFileResize);

  ResizeInline(layout);
}

void File::RefreshMetadata() const {
  if (metadata_refresher_)
    const_cast<File*>(this)->metadata_ = throw_if_error(metadata_refresher_());
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

void File::ResizeExternal(const FileLayout& layout) {
  assert(layout.category == FileLayout::CategoryFromValue(metadata()->size_category.value()));
  assert(layout.category == FileLayoutCategory::Blocks || layout.category == FileLayoutCategory::LargeBlocks ||
         layout.category == FileLayoutCategory::Clusters);

  const auto old_layout = CurrentLayout(metadata(), quota()->block_size_log2());
  const auto metadata_realigns = old_layout.metadata_log2_size != layout.metadata_log2_size ||
                                 old_layout.data_units_count != layout.data_units_count;
  if (metadata_realigns)
    FlushAndDetachExternalDataBlocks(old_layout);

  std::vector<uint32_t> allocated_blocks;
  if (layout.data_units_count > old_layout.data_units_count) {
    allocated_blocks = throw_if_error(quota()->AllocDataBlocks(layout.data_units_count - old_layout.data_units_count,
                                                               AllocationBlockType(layout.category)));
  }

  const auto removed_blocks = RemovedAllocationBlocks(metadata(), old_layout, layout);

  std::vector<std::byte> metadata_bytes(size_t{1} << layout.metadata_log2_size, std::byte{0});
  auto* updated_metadata = reinterpret_cast<EntryMetadata*>(metadata_bytes.data());
  std::memcpy(updated_metadata, metadata(), metadata()->size());
  updated_metadata->file_size = layout.file_size;
  updated_metadata->size_on_disk = layout.size_on_disk;
  updated_metadata->metadata_log2_size = layout.metadata_log2_size;
  updated_metadata->size_category = FileLayout::CategoryValue(layout.category);
  CopyExternalMetadataItems(metadata(), updated_metadata, old_layout, layout, allocated_blocks);

  try {
    ReplaceMetadata(updated_metadata);
  } catch (...) {
    FreeAllocatedDataBlocks(quota(), layout.category, allocated_blocks);
    throw;
  }

  ResizeExternalDataBlocks(old_layout.file_size, layout.file_size, layout);
  DetachAllocatedDataBlocks(quota(), old_layout.category, removed_blocks);
  FreeAllocatedDataBlocks(quota(), old_layout.category, removed_blocks);
}

void File::ResizeExternalDataBlocks(uint32_t old_file_size, uint32_t new_file_size, const FileLayout& layout) {
  if (old_file_size == new_file_size)
    return;

  const auto data_block_log2_size = DataBlockLog2Size(layout.category, quota()->block_size_log2());
  const auto data_block_size = size_t{1} << data_block_log2_size;

  if (new_file_size < old_file_size) {
    if (new_file_size == 0)
      return;
    const auto block_offset = floor_pow2(static_cast<size_t>(new_file_size - 1), data_block_log2_size);
    auto ref = GetExternalDataBlockRef(layout.category, metadata(), metadata_block(), quota()->block_size_log2(),
                                       block_offset, old_file_size);
    assert(ref.size > 0);
    auto block =
        throw_if_error(quota()->LoadDataBlock(ref.block_number, static_cast<BlockSize>(quota()->block_size_log2()),
                                              ref.block_type, ref.size, std::move(ref.hash), IsEncrypted()));
    block->Resize(DataSizeForBlock(new_file_size, block_offset, data_block_log2_size));
    return;
  }

  for (auto block_offset = floor_pow2(static_cast<size_t>(old_file_size), data_block_log2_size);
       block_offset < new_file_size; block_offset += data_block_size) {
    const auto old_data_size = DataSizeForBlock(old_file_size, block_offset, data_block_log2_size);
    const auto new_data_size = DataSizeForBlock(new_file_size, block_offset, data_block_log2_size);
    if (new_data_size == 0)
      continue;

    auto ref = GetExternalDataBlockRef(layout.category, metadata(), metadata_block(), quota()->block_size_log2(),
                                       block_offset, old_data_size == 0 ? new_file_size : old_file_size);
    auto block = throw_if_error(
        quota()->LoadDataBlock(ref.block_number, static_cast<BlockSize>(quota()->block_size_log2()), ref.block_type,
                               old_data_size == 0 ? new_data_size : old_data_size, std::move(ref.hash), IsEncrypted(),
                               /*new_block=*/old_data_size == 0));
    if (old_data_size == 0) {
      std::ranges::fill(block->mutable_data(), std::byte{0});
    } else if (new_data_size != old_data_size) {
      block->Resize(new_data_size);
    }
  }
}

void File::FlushAndDetachExternalDataBlocks(const FileLayout& layout) {
  const auto data_block_log2_size = DataBlockLog2Size(layout.category, quota()->block_size_log2());
  const auto data_block_size = size_t{1} << data_block_log2_size;
  for (size_t block_offset = 0; block_offset < layout.file_size; block_offset += data_block_size) {
    auto ref = GetExternalDataBlockRef(layout.category, metadata(), metadata_block(), quota()->block_size_log2(),
                                       block_offset, layout.file_size);
    if (ref.size == 0)
      continue;
    auto block =
        throw_if_error(quota()->LoadDataBlock(ref.block_number, static_cast<BlockSize>(quota()->block_size_log2()),
                                              ref.block_type, ref.size, std::move(ref.hash), IsEncrypted()));
    block->Flush();
    block->Detach();
  }
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
