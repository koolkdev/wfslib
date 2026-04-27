/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_resizer.h"

#include <algorithm>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "block.h"
#include "errors.h"
#include "file_data_units.h"
#include "file_layout_accessor.h"
#include "quota_area.h"
#include "utils.h"

namespace {
struct DataBlockCacheRef {
  // Old-cache identity captured before metadata replacement. Hash refs are intentionally omitted; post-commit detach
  // should only evict stale cached blocks, not flush through old hash locations.
  uint32_t block_number;
  BlockType block_type;
  uint32_t data_size;
};

struct DataUnitRef {
  uint32_t block_number;
  uint32_t blocks_count;
};

FileLayoutCategory CurrentCategory(const EntryMetadata* metadata) {
  return FileLayout::CategoryFromValue(metadata->size_category.value());
}

uint32_t UsedDataBlockSize(uint32_t file_size, size_t block_offset, size_t log2_block_size) {
  if (file_size <= block_offset)
    return 0;
  return static_cast<uint32_t>(std::min(size_t{1} << log2_block_size, size_t{file_size} - block_offset));
}

BlockType AllocationBlockType(FileLayoutCategory category) {
  switch (category) {
    case FileLayoutCategory::Blocks:
      return BlockType::Single;
    case FileLayoutCategory::LargeBlocks:
      return BlockType::Large;
    case FileLayoutCategory::Clusters:
    case FileLayoutCategory::ClusterMetadataBlocks:
      return BlockType::Cluster;
    case FileLayoutCategory::Inline:
    default:
      throw std::logic_error("Unexpected file resize layout state");
  }
}

std::vector<std::shared_ptr<Block>> LoadClusterMetadataBlocks(const std::shared_ptr<QuotaArea>& quota,
                                                              const EntryMetadata* metadata,
                                                              const FileLayout& layout,
                                                              uint8_t block_size_log2) {
  const auto metadata_blocks_count =
      FileLayout::MetadataItemsCount(layout.category, layout.size_on_disk, block_size_log2);
  auto block_refs = ClusterMetadataBlockRefs(metadata, metadata_blocks_count);
  std::vector<std::shared_ptr<Block>> blocks;
  blocks.reserve(metadata_blocks_count);
  for (const auto& block_ref : block_refs)
    blocks.push_back(throw_if_error(quota->LoadMetadataBlock(block_ref.value())));
  return blocks;
}

template <FileLayoutCategory Category>
FileLayout CurrentLayout(const EntryMetadata* metadata, uint8_t block_size_log2) {
  return FileLayout{
      .category = Category,
      .metadata_log2_size = metadata->metadata_log2_size.value(),
      .file_size = metadata->file_size.value(),
      .size_on_disk = metadata->size_on_disk.value(),
      .data_units_count = FileLayout::DataUnitsCount(Category, metadata->size_on_disk.value(), block_size_log2),
  };
}

FileLayout CurrentLayout(const EntryMetadata* metadata, uint8_t block_size_log2) {
  switch (CurrentCategory(metadata)) {
    case FileLayoutCategory::Inline:
      return FileLayout{
          .category = FileLayoutCategory::Inline,
          .metadata_log2_size = metadata->metadata_log2_size.value(),
          .file_size = metadata->file_size.value(),
          .size_on_disk = metadata->size_on_disk.value(),
          .data_units_count = 0,
      };
    case FileLayoutCategory::Blocks:
      return CurrentLayout<FileLayoutCategory::Blocks>(metadata, block_size_log2);
    case FileLayoutCategory::LargeBlocks:
      return CurrentLayout<FileLayoutCategory::LargeBlocks>(metadata, block_size_log2);
    case FileLayoutCategory::Clusters:
      return CurrentLayout<FileLayoutCategory::Clusters>(metadata, block_size_log2);
    case FileLayoutCategory::ClusterMetadataBlocks:
      return CurrentLayout<FileLayoutCategory::ClusterMetadataBlocks>(metadata, block_size_log2);
    default:
      throw std::logic_error("Unexpected file resize layout state");
  }
}

class AllocatedTargetDataUnits {
 public:
  AllocatedTargetDataUnits(std::shared_ptr<QuotaArea> quota, const FileLayout& layout) : quota_(std::move(quota)) {
    if (layout.data_units_count == 0)
      return;

    const auto block_type = AllocationBlockType(layout.category);
    block_numbers_ = throw_if_error(quota_->AllocDataBlocks(layout.data_units_count, block_type));
    blocks_count_ = uint32_t{1} << log2_size(block_type);
  }

  ~AllocatedTargetDataUnits() {
    // Until metadata replacement succeeds, newly allocated data units are rollback state.
    if (!released_) {
      for (const auto block_number : block_numbers_)
        quota_->DeleteBlocks(block_number, blocks_count_);
    }
  }

  const std::vector<uint32_t>& block_numbers() const { return block_numbers_; }

  void release() { released_ = true; }

 private:
  std::shared_ptr<QuotaArea> quota_;
  std::vector<uint32_t> block_numbers_;
  uint32_t blocks_count_{0};
  bool released_{false};
};

class AllocatedTargetMetadataBlocks {
 public:
  AllocatedTargetMetadataBlocks(std::shared_ptr<QuotaArea> quota, const FileLayout& layout) : quota_(std::move(quota)) {
    if (layout.category != FileLayoutCategory::ClusterMetadataBlocks)
      return;

    const auto metadata_blocks_count = FileLayout::MetadataItemsCount(layout.category, layout.size_on_disk,
                                                                      static_cast<uint8_t>(quota_->block_size_log2()));
    blocks_.reserve(metadata_blocks_count);
    block_numbers_.reserve(metadata_blocks_count);
    for (uint32_t i = 0; i < metadata_blocks_count; ++i) {
      auto block = quota_->AllocMetadataBlock();
      if (!block.has_value()) {
        if (block.error() == WfsError::kNoSpace)
          Rollback();
        throw WfsException(block.error());
      }

      std::ranges::fill((*block)->mutable_data(), std::byte{0});
      block_numbers_.push_back(quota_->to_area_block_number((*block)->physical_block_number()));
      blocks_.push_back(std::move(*block));
    }
  }

  ~AllocatedTargetMetadataBlocks() {
    // Until metadata replacement succeeds, newly allocated metadata blocks are rollback state.
    if (!released_)
      Rollback();
  }

  const std::vector<std::shared_ptr<Block>>& blocks() const { return blocks_; }
  const std::vector<uint32_t>& block_numbers() const { return block_numbers_; }

  void Flush() const {
    for (const auto& block : blocks_)
      block->Flush();
  }

  void release() { released_ = true; }

 private:
  void Rollback() {
    for (const auto& block : blocks_) {
      quota_->DeleteBlocks(quota_->to_area_block_number(block->physical_block_number()), 1);
      block->Detach();
    }
    blocks_.clear();
    block_numbers_.clear();
  }

  std::shared_ptr<QuotaArea> quota_;
  std::vector<std::shared_ptr<Block>> blocks_;
  std::vector<uint32_t> block_numbers_;
  bool released_{false};
};

template <FileLayoutCategory Category>
void StoreAllocatedDataUnits(EntryMetadata* metadata, std::span<const uint32_t> block_numbers) {
  using Traits = FileDataUnitLayoutTraits<Category>;
  using Metadata = typename Traits::Metadata;

  std::vector<Metadata> data_units(block_numbers.size());
  for (auto&& [data_unit, block_number] : std::views::zip(data_units, block_numbers))
    Traits::set_unit_block_number(data_unit, block_number);

  auto stored_metadata = MutableFileDataUnitLogicalMetadataItems<Category>(metadata, data_units.size());
  std::ranges::copy(data_units, stored_metadata.begin());
}

void StoreAllocatedDataUnits(FileLayoutCategory category,
                             EntryMetadata* metadata,
                             std::span<const uint32_t> block_numbers,
                             const AllocatedTargetMetadataBlocks& metadata_blocks,
                             uint8_t block_size_log2) {
  switch (category) {
    case FileLayoutCategory::Blocks:
      StoreAllocatedDataUnits<FileLayoutCategory::Blocks>(metadata, block_numbers);
      return;
    case FileLayoutCategory::LargeBlocks:
      StoreAllocatedDataUnits<FileLayoutCategory::LargeBlocks>(metadata, block_numbers);
      return;
    case FileLayoutCategory::Clusters:
      StoreAllocatedDataUnits<FileLayoutCategory::Clusters>(metadata, block_numbers);
      return;
    case FileLayoutCategory::Inline:
      return;
    case FileLayoutCategory::ClusterMetadataBlocks: {
      // Category 4 has two allocation layers: entry metadata points at metadata blocks, and those metadata blocks
      // point at the actual data clusters.
      auto entry_refs = MutableClusterMetadataBlockRefs(metadata, metadata_blocks.block_numbers().size());
      std::ranges::copy(metadata_blocks.block_numbers(), entry_refs.begin());

      const auto clusters_per_metadata_block = FileLayout::ClustersPerClusterMetadataBlock(block_size_log2);
      for (size_t cluster_index = 0; cluster_index < block_numbers.size(); ++cluster_index) {
        auto cluster_metadata = MutableClusterMetadataBlockItems(
            metadata_blocks.blocks()[cluster_index / clusters_per_metadata_block], clusters_per_metadata_block);
        cluster_metadata[cluster_index % clusters_per_metadata_block].block_number = block_numbers[cluster_index];
      }
      return;
    }
    default:
      throw std::logic_error("Unexpected file resize layout state");
  }
}

template <FileLayoutCategory Category>
std::vector<DataBlockCacheRef> DataBlockCacheRefs(const std::shared_ptr<Block>& metadata_block,
                                                  const EntryMetadata* metadata,
                                                  const FileLayout& layout,
                                                  uint8_t block_size_log2) {
  const auto data_block_log2_size = FileDataBlockLog2Size<Category>(block_size_log2);
  const auto data_blocks_count = div_ceil(layout.file_size, size_t{1} << data_block_log2_size);
  std::vector<DataBlockCacheRef> refs;
  refs.reserve(data_blocks_count);

  for (const auto data_block_index : std::views::iota(size_t{0}, data_blocks_count)) {
    const auto block_offset = data_block_index << data_block_log2_size;
    const auto data_size = UsedDataBlockSize(layout.file_size, block_offset, data_block_log2_size);
    auto location = FileDataBlockLocationFor<Category>(metadata_block, metadata, data_block_index, block_size_log2);
    refs.push_back({location.block_number, location.block_type, data_size});
  }

  return refs;
}

std::vector<DataBlockCacheRef> ClusterMetadataBlocksDataBlockCacheRefs(const std::shared_ptr<QuotaArea>& quota,
                                                                       const EntryMetadata* metadata,
                                                                       const FileLayout& layout,
                                                                       uint8_t block_size_log2) {
  const auto metadata_blocks = LoadClusterMetadataBlocks(quota, metadata, layout, block_size_log2);
  const auto data_block_log2_size = FileDataBlockLog2Size<FileLayoutCategory::Clusters>(block_size_log2);
  const auto data_blocks_count = div_ceil(layout.file_size, size_t{1} << data_block_log2_size);
  std::vector<DataBlockCacheRef> refs;
  refs.reserve(data_blocks_count);

  for (const auto data_block_index : std::views::iota(size_t{0}, data_blocks_count)) {
    const auto block_offset = data_block_index << data_block_log2_size;
    const auto data_size = UsedDataBlockSize(layout.file_size, block_offset, data_block_log2_size);
    auto location = ClusterMetadataBlocksDataBlockLocationFor(metadata_blocks, data_block_index, block_size_log2);
    refs.push_back({location.block_number, location.block_type, data_size});
  }

  return refs;
}

std::vector<DataBlockCacheRef> DataBlockCacheRefs(const std::shared_ptr<QuotaArea>& quota,
                                                  const std::shared_ptr<Block>& metadata_block,
                                                  const EntryMetadata* metadata,
                                                  const FileLayout& layout,
                                                  uint8_t block_size_log2) {
  switch (layout.category) {
    case FileLayoutCategory::Inline:
      return {};
    case FileLayoutCategory::Blocks:
      return DataBlockCacheRefs<FileLayoutCategory::Blocks>(metadata_block, metadata, layout, block_size_log2);
    case FileLayoutCategory::LargeBlocks:
      return DataBlockCacheRefs<FileLayoutCategory::LargeBlocks>(metadata_block, metadata, layout, block_size_log2);
    case FileLayoutCategory::Clusters:
      return DataBlockCacheRefs<FileLayoutCategory::Clusters>(metadata_block, metadata, layout, block_size_log2);
    case FileLayoutCategory::ClusterMetadataBlocks:
      return ClusterMetadataBlocksDataBlockCacheRefs(quota, metadata, layout, block_size_log2);
    default:
      throw std::logic_error("Unexpected file resize layout state");
  }
}

template <FileLayoutCategory Category>
std::vector<DataUnitRef> DataUnitRefs(const EntryMetadata* metadata, uint32_t data_units_count) {
  using Traits = FileDataUnitLayoutTraits<Category>;

  auto metadata_items = FileDataUnitLogicalMetadataItems<Category>(metadata, data_units_count);
  return metadata_items | std::views::transform([](const auto& metadata) {
           return DataUnitRef{Traits::unit_block_number(metadata), FileDataUnitAreaBlocksCount<Category>()};
         }) |
         std::ranges::to<std::vector>();
}

std::vector<DataUnitRef> ClusterMetadataBlocksDataUnitRefs(const std::shared_ptr<QuotaArea>& quota,
                                                           const EntryMetadata* metadata,
                                                           const FileLayout& layout,
                                                           uint8_t block_size_log2) {
  const auto metadata_blocks = LoadClusterMetadataBlocks(quota, metadata, layout, block_size_log2);
  const auto clusters_per_metadata_block = FileLayout::ClustersPerClusterMetadataBlock(block_size_log2);
  std::vector<DataUnitRef> refs;
  refs.reserve(layout.data_units_count);

  for (size_t cluster_index = 0; cluster_index < layout.data_units_count; ++cluster_index) {
    auto cluster_metadata = ClusterMetadataBlockItems(metadata_blocks[cluster_index / clusters_per_metadata_block],
                                                      clusters_per_metadata_block);
    refs.push_back({cluster_metadata[cluster_index % clusters_per_metadata_block].block_number.value(),
                    FileDataUnitAreaBlocksCount<FileLayoutCategory::Clusters>()});
  }

  return refs;
}

std::vector<DataUnitRef> DataUnitRefs(const std::shared_ptr<QuotaArea>& quota,
                                      const EntryMetadata* metadata,
                                      const FileLayout& layout,
                                      uint8_t block_size_log2) {
  switch (layout.category) {
    case FileLayoutCategory::Inline:
      return {};
    case FileLayoutCategory::Blocks:
      return DataUnitRefs<FileLayoutCategory::Blocks>(metadata, layout.data_units_count);
    case FileLayoutCategory::LargeBlocks:
      return DataUnitRefs<FileLayoutCategory::LargeBlocks>(metadata, layout.data_units_count);
    case FileLayoutCategory::Clusters:
      return DataUnitRefs<FileLayoutCategory::Clusters>(metadata, layout.data_units_count);
    case FileLayoutCategory::ClusterMetadataBlocks:
      return ClusterMetadataBlocksDataUnitRefs(quota, metadata, layout, block_size_log2);
    default:
      throw std::logic_error("Unexpected file resize layout state");
  }
}

std::vector<DataUnitRef> ClusterMetadataBlockUnitRefs(const EntryMetadata* metadata,
                                                      const FileLayout& layout,
                                                      uint8_t block_size_log2) {
  if (layout.category != FileLayoutCategory::ClusterMetadataBlocks)
    return {};

  const auto metadata_blocks_count =
      FileLayout::MetadataItemsCount(layout.category, layout.size_on_disk, block_size_log2);
  auto block_refs = ClusterMetadataBlockRefs(metadata, metadata_blocks_count);
  return block_refs |
         std::views::transform([](const auto& block_number) { return DataUnitRef{block_number.value(), 1}; }) |
         std::ranges::to<std::vector>();
}

void DetachDataBlocks(const std::shared_ptr<QuotaArea>& quota,
                      std::span<const DataBlockCacheRef> refs,
                      bool encrypted,
                      uint8_t block_size_log2) {
  // After metadata replacement, cached blocks still point at old hash refs and may have obsolete used sizes. Load
  // cache entries without touching disk so every old block object is detached and future I/O reloads from new metadata.
  for (const auto& ref : refs) {
    auto data_block =
        throw_if_error(quota->LoadDataBlock(ref.block_number, static_cast<BlockSize>(block_size_log2), ref.block_type,
                                            ref.data_size, Block::HashRef{}, encrypted, /*new_block=*/true));
    data_block->Detach();
  }
}

void DetachMetadataBlocks(const std::shared_ptr<QuotaArea>& quota, std::span<const DataUnitRef> refs) {
  for (const auto& ref : refs) {
    auto metadata_block = throw_if_error(quota->LoadMetadataBlock(ref.block_number, /*new_block=*/true));
    metadata_block->Detach();
  }
}

void FreeDataUnits(const std::shared_ptr<QuotaArea>& quota, std::span<const DataUnitRef> refs) {
  for (const auto& ref : refs) {
    if (!quota->DeleteBlocks(ref.block_number, ref.blocks_count))
      throw WfsException(WfsError::kFreeBlocksAllocatorCorrupted);
  }
}

template <typename SourceAccessor>
void ReadExact(SourceAccessor& source, std::byte* output, size_t offset, size_t size) {
  // LayoutAccessor::Read stops at the current source block. Transitions may copy many small source blocks into one
  // larger target block, so keep reading until the requested range is complete.
  size_t copied = 0;
  while (copied != size) {
    const auto read = source.Read(output + copied, offset + copied, size - copied);
    if (read == 0)
      throw std::runtime_error("Failed to copy file layout data");
    copied += read;
  }
}

template <typename SourceAccessor>
void CopyToInlineLayout(SourceAccessor& source, EntryMetadata* target_metadata, size_t bytes_to_preserve) {
  auto target_data = std::span{reinterpret_cast<std::byte*>(target_metadata) + target_metadata->size(),
                               target_metadata->size_on_disk.value()};
  if (bytes_to_preserve == 0)
    return;
  ReadExact(source, target_data.data(), 0, bytes_to_preserve);
}

template <FileLayoutCategory Category, typename SourceAccessor>
void CopyToDataUnitLayout(SourceAccessor& source,
                          const std::shared_ptr<QuotaArea>& quota,
                          const EntryMetadataReplacement& replacement,
                          const FileLayout& target_layout,
                          size_t bytes_to_preserve,
                          bool encrypted,
                          uint8_t block_size_log2) {
  const auto data_block_log2_size = FileDataBlockLog2Size<Category>(block_size_log2);
  const auto data_blocks_count = div_ceil(target_layout.file_size, size_t{1} << data_block_log2_size);

  for (const auto data_block_index : std::views::iota(size_t{0}, data_blocks_count)) {
    const auto block_offset = data_block_index << data_block_log2_size;
    const auto data_size = UsedDataBlockSize(target_layout.file_size, block_offset, data_block_log2_size);
    auto location =
        FileDataBlockLocationFor<Category>(replacement.block(), replacement.get(), data_block_index, block_size_log2);
    auto data_block =
        throw_if_error(quota->LoadDataBlock(location.block_number, static_cast<BlockSize>(block_size_log2),
                                            location.block_type, data_size, std::move(location.hash), encrypted,
                                            /*new_block=*/true));

    auto data = data_block->mutable_data();
    std::ranges::fill(data, std::byte{0});
    if (block_offset < bytes_to_preserve) {
      const auto bytes_to_copy = std::min<size_t>(data_size, bytes_to_preserve - block_offset);
      ReadExact(source, data.data(), block_offset, bytes_to_copy);
    }

    data_block->Flush();
    data_block->Detach();
  }
}

template <typename SourceAccessor>
void CopyToClusterMetadataBlocksLayout(SourceAccessor& source,
                                       const std::shared_ptr<QuotaArea>& quota,
                                       const AllocatedTargetMetadataBlocks& metadata_blocks,
                                       const FileLayout& target_layout,
                                       size_t bytes_to_preserve,
                                       bool encrypted,
                                       uint8_t block_size_log2) {
  const auto data_block_log2_size = FileDataBlockLog2Size<FileLayoutCategory::Clusters>(block_size_log2);
  const auto data_blocks_count = div_ceil(target_layout.file_size, size_t{1} << data_block_log2_size);

  for (const auto data_block_index : std::views::iota(size_t{0}, data_blocks_count)) {
    const auto block_offset = data_block_index << data_block_log2_size;
    const auto data_size = UsedDataBlockSize(target_layout.file_size, block_offset, data_block_log2_size);
    auto location =
        ClusterMetadataBlocksDataBlockLocationFor(metadata_blocks.blocks(), data_block_index, block_size_log2);
    auto data_block =
        throw_if_error(quota->LoadDataBlock(location.block_number, static_cast<BlockSize>(block_size_log2),
                                            location.block_type, data_size, std::move(location.hash), encrypted,
                                            /*new_block=*/true));

    auto data = data_block->mutable_data();
    std::ranges::fill(data, std::byte{0});
    if (block_offset < bytes_to_preserve) {
      const auto bytes_to_copy = std::min<size_t>(data_size, bytes_to_preserve - block_offset);
      ReadExact(source, data.data(), block_offset, bytes_to_copy);
    }

    data_block->Flush();
    data_block->Detach();
  }

  metadata_blocks.Flush();
}

template <typename SourceAccessor>
void CopyToTargetLayout(SourceAccessor& source,
                        const std::shared_ptr<QuotaArea>& quota,
                        EntryMetadataReplacement& replacement,
                        const AllocatedTargetMetadataBlocks& metadata_blocks,
                        const FileLayout& target_layout,
                        size_t bytes_to_preserve,
                        bool encrypted,
                        uint8_t block_size_log2) {
  switch (target_layout.category) {
    case FileLayoutCategory::Inline:
      CopyToInlineLayout(source, replacement.get(), bytes_to_preserve);
      return;
    case FileLayoutCategory::Blocks:
      CopyToDataUnitLayout<FileLayoutCategory::Blocks>(source, quota, replacement, target_layout, bytes_to_preserve,
                                                       encrypted, block_size_log2);
      return;
    case FileLayoutCategory::LargeBlocks:
      CopyToDataUnitLayout<FileLayoutCategory::LargeBlocks>(source, quota, replacement, target_layout,
                                                            bytes_to_preserve, encrypted, block_size_log2);
      return;
    case FileLayoutCategory::Clusters:
      CopyToDataUnitLayout<FileLayoutCategory::Clusters>(source, quota, replacement, target_layout, bytes_to_preserve,
                                                         encrypted, block_size_log2);
      return;
    case FileLayoutCategory::ClusterMetadataBlocks:
      CopyToClusterMetadataBlocksLayout(source, quota, metadata_blocks, target_layout, bytes_to_preserve, encrypted,
                                        block_size_log2);
      return;
    default:
      throw std::logic_error("Unexpected file resize layout state");
  }
}
}  // namespace

void FileResizer::ResizeViaLayoutRebuild(const FileLayout& target_layout) {
  const auto* metadata = file_->metadata();
  const auto old_layout = CurrentLayout(metadata, file_->quota()->block_size_log2());

  const auto block_size_log2 = file_->quota()->block_size_log2();
  const auto encrypted = !(metadata->flags.value() & EntryMetadata::UNENCRYPTED_FILE);
  const auto bytes_to_preserve = std::min(old_layout.file_size, target_layout.file_size);
  // Category 4 uses this path even for same-category resize because the two-level metadata makes in-place mutation
  // considerably riskier. Rebuilding keeps the commit point the same as cross-category resize.
  auto source = File::CreateLayoutAccessor(file_);

  const auto old_data_blocks =
      DataBlockCacheRefs(file_->quota(), file_->metadata_block(), metadata, old_layout, block_size_log2);
  const auto old_data_units = DataUnitRefs(file_->quota(), metadata, old_layout, block_size_log2);
  const auto old_metadata_blocks = ClusterMetadataBlockUnitRefs(metadata, old_layout, block_size_log2);

  EntryMetadataReplacement replacement(metadata, target_layout);
  AllocatedTargetDataUnits allocated_units(file_->quota(), target_layout);
  AllocatedTargetMetadataBlocks allocated_metadata_blocks(file_->quota(), target_layout);
  StoreAllocatedDataUnits(target_layout.category, replacement.get(), allocated_units.block_numbers(),
                          allocated_metadata_blocks, block_size_log2);

  CopyToTargetLayout(*source, file_->quota(), replacement, allocated_metadata_blocks, target_layout, bytes_to_preserve,
                     encrypted, block_size_log2);

  // Commit point: target data and hashes are already durable but not referenced until this metadata replacement.
  ReplaceMetadata(replacement.get());
  allocated_units.release();
  allocated_metadata_blocks.release();

  DetachDataBlocks(file_->quota(), old_data_blocks, encrypted, block_size_log2);
  DetachMetadataBlocks(file_->quota(), old_metadata_blocks);
  FreeDataUnits(file_->quota(), old_data_units);
  FreeDataUnits(file_->quota(), old_metadata_blocks);
}
