/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_resizer.h"

#include <algorithm>
#include <cassert>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "block.h"
#include "errors.h"
#include "file_data_units.h"
#include "quota_area.h"
#include "utils.h"

namespace {
[[maybe_unused]] FileLayoutCategory CurrentCategory(const EntryMetadata* metadata) {
  return FileLayout::CategoryFromValue(metadata->size_category.value());
}

struct DataBlockCacheRef {
  // Old-cache identity captured before metadata replacement. Hash refs are intentionally omitted; post-commit detach
  // should only evict stale cached blocks, not flush through old hash locations.
  uint32_t block_number;
  BlockType block_type;
  uint32_t data_size;
};

uint32_t UsedDataBlockSize(uint32_t file_size, size_t block_offset, size_t log2_block_size) {
  if (file_size <= block_offset)
    return 0;
  return static_cast<uint32_t>(std::min(size_t{1} << log2_block_size, size_t{file_size} - block_offset));
}

template <FileLayoutCategory Category>
class AllocatedDataUnits {
 public:
  AllocatedDataUnits(std::shared_ptr<QuotaArea> quota, uint32_t count)
      : quota_(std::move(quota)),
        block_numbers_(count == 0 ? std::vector<uint32_t>{}
                                  : throw_if_error(quota_->AllocDataBlocks(
                                        count,
                                        FileDataUnitLayoutTraits<Category>::kAllocationBlockType))) {}

  ~AllocatedDataUnits() {
    // Until metadata replacement succeeds, newly allocated data units are rollback state.
    if (!released_) {
      for (const auto block_number : block_numbers_)
        quota_->DeleteBlocks(block_number, FileDataUnitAreaBlocksCount<Category>());
    }
  }

  const std::vector<uint32_t>& block_numbers() const { return block_numbers_; }

  void release() { released_ = true; }

 private:
  std::shared_ptr<QuotaArea> quota_;
  std::vector<uint32_t> block_numbers_;
  bool released_{false};
};

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

template <FileLayoutCategory Category>
std::vector<typename FileDataUnitLayoutTraits<Category>::Metadata> LogicalMetadata(const EntryMetadata* metadata,
                                                                                   size_t count) {
  using Metadata = typename FileDataUnitLayoutTraits<Category>::Metadata;
  return FileDataUnitLogicalMetadataItems<Category>(metadata, count) | std::ranges::to<std::vector<Metadata>>();
}

template <FileLayoutCategory Category>
void StoreLogicalMetadata(EntryMetadata* metadata,
                          std::span<const typename FileDataUnitLayoutTraits<Category>::Metadata> logical_metadata) {
  auto stored_metadata = MutableFileDataUnitLogicalMetadataItems<Category>(metadata, logical_metadata.size());
  std::ranges::copy(logical_metadata, stored_metadata.begin());
}

template <FileLayoutCategory Category>
std::vector<typename FileDataUnitLayoutTraits<Category>::Metadata> BuildReplacementUnitMetadata(
    const std::vector<typename FileDataUnitLayoutTraits<Category>::Metadata>& old_metadata,
    uint32_t target_units_count,
    const std::vector<uint32_t>& allocated_block_numbers) {
  using Traits = FileDataUnitLayoutTraits<Category>;

  auto replacement_metadata = old_metadata;
  replacement_metadata.resize(target_units_count);

  auto appended_metadata = replacement_metadata | std::views::drop(old_metadata.size());
  for (auto&& [metadata, block_number] : std::views::zip(appended_metadata, allocated_block_numbers))
    Traits::set_unit_block_number(metadata, block_number);

  return replacement_metadata;
}

template <FileLayoutCategory Category>
std::shared_ptr<Block> LoadResizableDataBlock(const std::shared_ptr<QuotaArea>& quota,
                                              const std::shared_ptr<Block>& metadata_block,
                                              EntryMetadata* metadata,
                                              size_t data_block_index,
                                              uint32_t data_size,
                                              bool encrypted,
                                              bool new_block,
                                              uint8_t block_size_log2) {
  auto location = FileDataBlockLocationFor<Category>(metadata_block, metadata, data_block_index, block_size_log2);
  return throw_if_error(quota->LoadDataBlock(location.block_number, static_cast<BlockSize>(block_size_log2),
                                             location.block_type, data_size, std::move(location.hash), encrypted,
                                             new_block));
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

template <FileLayoutCategory Category>
void FlushRetainedDataBlocks(const std::shared_ptr<QuotaArea>& quota,
                             const std::shared_ptr<Block>& metadata_block,
                             EntryMetadata* metadata,
                             const FileLayout& old_layout,
                             const FileLayout& target_layout,
                             bool encrypted,
                             uint8_t block_size_log2) {
  // Before the commit point, only retained blocks are flushed. Dropped blocks must stay attached because a failed
  // metadata replacement leaves the old layout authoritative.
  const auto data_block_log2_size = FileDataBlockLog2Size<Category>(block_size_log2);
  const auto data_blocks_count = div_ceil(old_layout.file_size, size_t{1} << data_block_log2_size);
  for (const auto data_block_index : std::views::iota(size_t{0}, data_blocks_count)) {
    const auto block_offset = data_block_index << data_block_log2_size;
    const auto target_data_size = UsedDataBlockSize(target_layout.file_size, block_offset, data_block_log2_size);
    if (target_data_size == 0)
      continue;

    const auto data_size = UsedDataBlockSize(old_layout.file_size, block_offset, data_block_log2_size);
    auto data_block = LoadResizableDataBlock<Category>(quota, metadata_block, metadata, data_block_index, data_size,
                                                       encrypted, /*new_block=*/false, block_size_log2);
    data_block->Flush();
  }
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

template <FileLayoutCategory Category>
void ResizeChangedDataBlocks(const std::shared_ptr<QuotaArea>& quota,
                             const std::shared_ptr<Block>& metadata_block,
                             EntryMetadata* metadata,
                             const FileLayout& old_layout,
                             const FileLayout& target_layout,
                             bool encrypted,
                             uint8_t block_size_log2) {
  const auto data_block_log2_size = FileDataBlockLog2Size<Category>(block_size_log2);
  const auto data_blocks_count =
      div_ceil(std::max(old_layout.file_size, target_layout.file_size), size_t{1} << data_block_log2_size);
  for (const auto data_block_index : std::views::iota(size_t{0}, data_blocks_count)) {
    const auto block_offset = data_block_index << data_block_log2_size;
    const auto old_data_size = UsedDataBlockSize(old_layout.file_size, block_offset, data_block_log2_size);
    const auto new_data_size = UsedDataBlockSize(target_layout.file_size, block_offset, data_block_log2_size);
    if (old_data_size == new_data_size || new_data_size == 0)
      continue;

    auto data_block = LoadResizableDataBlock<Category>(quota, metadata_block, metadata, data_block_index,
                                                       old_data_size == 0 ? new_data_size : old_data_size, encrypted,
                                                       /*new_block=*/old_data_size == 0, block_size_log2);
    if (old_data_size == 0) {
      std::ranges::fill(data_block->mutable_data(), std::byte{0});
    } else {
      data_block->Resize(new_data_size);
    }
  }
}

template <FileLayoutCategory Category>
void FreeRemovedDataUnits(const std::shared_ptr<QuotaArea>& quota,
                          std::span<const typename FileDataUnitLayoutTraits<Category>::Metadata> old_metadata,
                          uint32_t target_units_count) {
  using Traits = FileDataUnitLayoutTraits<Category>;

  for (const auto& metadata : old_metadata | std::views::drop(target_units_count)) {
    if (!quota->DeleteBlocks(Traits::unit_block_number(metadata), FileDataUnitAreaBlocksCount<Category>()))
      throw WfsException(WfsError::kFreeBlocksAllocatorCorrupted);
  }
}
}  // namespace

template <FileLayoutCategory Category>
void FileResizer::ResizeDataUnitLayout(const FileLayout& target_layout) {
  const auto* metadata = file_->metadata();
  assert(CurrentCategory(metadata) == Category);
  assert(target_layout.category == Category);

  const auto block_size_log2 = file_->quota()->block_size_log2();
  const auto old_layout = CurrentLayout<Category>(metadata, block_size_log2);
  const auto encrypted = !(metadata->flags.value() & EntryMetadata::UNENCRYPTED_FILE);

  const auto old_data_blocks =
      DataBlockCacheRefs<Category>(file_->metadata_block(), metadata, old_layout, block_size_log2);
  FlushRetainedDataBlocks<Category>(file_->quota(), file_->metadata_block(), file_->mutable_metadata(), old_layout,
                                    target_layout, encrypted, block_size_log2);

  const auto old_metadata = LogicalMetadata<Category>(metadata, old_layout.data_units_count);
  const auto allocated_units_count = target_layout.data_units_count > old_layout.data_units_count
                                         ? target_layout.data_units_count - old_layout.data_units_count
                                         : 0;
  AllocatedDataUnits<Category> allocated_units(file_->quota(), allocated_units_count);
  EntryMetadataReplacement replacement(metadata, target_layout);

  const auto replacement_metadata = BuildReplacementUnitMetadata<Category>(old_metadata, target_layout.data_units_count,
                                                                           allocated_units.block_numbers());
  StoreLogicalMetadata<Category>(replacement.get(), replacement_metadata);

  // Commit point: after this succeeds, the replacement metadata is authoritative and old caches/units can be dropped.
  ReplaceMetadata(replacement.get());
  allocated_units.release();

  DetachDataBlocks(file_->quota(), old_data_blocks, encrypted, block_size_log2);
  ResizeChangedDataBlocks<Category>(file_->quota(), file_->metadata_block(), file_->mutable_metadata(), old_layout,
                                    target_layout, encrypted, block_size_log2);
  FreeRemovedDataUnits<Category>(file_->quota(), old_metadata, target_layout.data_units_count);
}

template void FileResizer::ResizeDataUnitLayout<FileLayoutCategory::Blocks>(const FileLayout& target_layout);
template void FileResizer::ResizeDataUnitLayout<FileLayoutCategory::LargeBlocks>(const FileLayout& target_layout);
template void FileResizer::ResizeDataUnitLayout<FileLayoutCategory::Clusters>(const FileLayout& target_layout);
