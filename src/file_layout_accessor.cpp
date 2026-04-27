/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file_layout_accessor.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <stdexcept>

#include "file_data_units.h"

namespace {
FileLayoutCategory CurrentCategory(const EntryMetadata* metadata) {
  return FileLayout::CategoryFromValue(metadata->size_category.value());
}
}  // namespace

// Category 0 - File data is in the attribute metadata (limited to 512 bytes minus attribute size) (no minumum)
class File::InlineLayoutAccessor : public File::LayoutAccessor {
 public:
  InlineLayoutAccessor(const std::shared_ptr<File>& file) : LayoutAccessor(file) {}

  size_t GetMetadataSize() const override { return GetMetadataItemsCount() * sizeof(std::byte); }

  std::span<const std::byte> GetData(size_t offset, size_t size) override {
    return InlinePayload().subspan(offset, size);
  }
  std::span<std::byte> GetMutableData(size_t offset, size_t size) override {
    return MutableInlinePayload().subspan(offset, size);
  }

  void Resize(size_t new_size) override {
    // Just update the attribute, the data in the metadata block
    file_->mutable_metadata()->file_size = static_cast<uint32_t>(new_size);
  }
};

class File::BlockListLayoutAccessor : public File::LayoutAccessor {
 public:
  BlockListLayoutAccessor(const std::shared_ptr<File>& file) : LayoutAccessor(file) {}

  size_t GetMetadataSize() const override { return GetMetadataItemsCount() * sizeof(DataBlockMetadata); }

  std::span<const std::byte> GetData(size_t offset, size_t size) override {
    auto data_ref = GetDataRef(offset, size);
    return data_ref.data_block->data().subspan(data_ref.offset_in_block, data_ref.size);
  }

  std::span<std::byte> GetMutableData(size_t offset, size_t size) override {
    auto data_ref = GetDataRef(offset, size);
    return data_ref.data_block->mutable_data().subspan(data_ref.offset_in_block, data_ref.size);
  }

  DataRef GetDataRef(size_t offset, size_t size) override {
    return GetDataRef(offset, size, file_->metadata()->file_size.value());
  }

  std::vector<DataBlockRef> EnumerateBlocks() const override {
    return EnumerateDataBlockRefs(DataBlockRefs(), /*start_offset=*/0, file_->metadata()->size_on_disk.value(),
                                  GetDataBlockType(), GetDataBlockSize());
  }

  void ResizeLastBlock(size_t file_size) override {
    if (file_size == 0)
      return;

    auto data_ref = GetDataRef(file_size - 1, 1, file_size);
    data_ref.data_block->Resize(static_cast<uint32_t>(data_ref.offset_in_block + 1));
  }

  DataRef GetDataFromBlock(DataBlockRef block_ref, size_t offset_in_block, size_t size) {
    LoadDataBlock(block_ref.block_number, static_cast<uint32_t>(block_ref.size), std::move(block_ref.hash));
    size = std::min(size, current_data_block->size() - offset_in_block);
    return {current_data_block, offset_in_block, size};
  }

  virtual DataRef GetDataRef(size_t offset, size_t size, size_t file_size) {
    auto block_position = BlockPositionForOffset(offset, GetDataBlockSize());
    auto location =
        FileDataBlockLocationFor(CurrentCategory(file_->metadata()), file_->metadata_block(), file_->metadata(),
                                 block_position.index, file_->quota()->block_size_log2());
    auto block_ref =
        DataBlockRef{location.block_number, location.block_type, block_position.offset,
                     DataSizeForBlock(file_size, block_position.offset, GetDataBlockSize()), std::move(location.hash)};
    return GetDataFromBlock(std::move(block_ref), block_position.offset_in_block, size);
  }

  void Resize(size_t new_size) override {
    size_t old_size = file_->metadata()->file_size.value();
    while (old_size != new_size) {
      std::shared_ptr<Block> current_block;
      size_t new_block_size = 0;
      if (new_size < old_size) {
        // Just update last block
        if (new_size > 0) {
          // Minus 1 because if it is right at the end of the block, we will get the next block
          auto chunk_info = GetDataRef(new_size - 1, 1);
          current_block = chunk_info.data_block;
          new_block_size = std::min(chunk_info.offset_in_block + 1, size_t{1} << GetDataBlockSize());
        }
        old_size = new_size;
      } else {
        if (old_size & ((1 << GetDataBlockSize()) - 1)) {
          // We need to incrase the size of the last block
          // Minus 1 because if it is right at the end of the block, we will get the next block
          auto chunk_info = GetDataRef(old_size - 1, 1);
          current_block = chunk_info.data_block;
          new_block_size =
              std::min(chunk_info.offset_in_block + 1 + (new_size - old_size), size_t{1} << GetDataBlockSize());
          old_size += new_block_size - (chunk_info.offset_in_block + 1);
        } else {
          // Open new block, the size of the loaded block will be 0
          auto chunk_info = GetDataRef(old_size, 0);
          current_block = chunk_info.data_block;
          assert(chunk_info.offset_in_block == 0);
          new_block_size = std::min(new_size - old_size, size_t{1} << GetDataBlockSize());
          old_size += new_block_size;
        }
      }
      file_->mutable_metadata()->file_size = static_cast<uint32_t>(old_size);
      if (current_block) {
        current_block->Resize(static_cast<uint32_t>(new_block_size));
      }
    }
  }

 protected:
  virtual BlockType GetDataBlockType() const = 0;
  virtual size_t GetDataBlockSize() const { return DataBlockLog2Size(GetDataBlockType()); }
  std::shared_ptr<Block> current_data_block;

  template <typename DataBlocks>
  std::vector<DataBlockRef> EnumerateDataBlockRefs(DataBlocks&& blocks,
                                                   size_t start_offset,
                                                   size_t size,
                                                   BlockType block_type,
                                                   size_t log2_block_size) const {
    std::vector<DataBlockRef> refs;
    refs.reserve(blocks.size());

    size_t block_offset = start_offset;
    for (const auto& block : blocks) {
      if (block_offset >= start_offset + size)
        break;
      refs.push_back({block.block_number.value(), block_type, block_offset,
                      DataSizeForBlock(start_offset + size, block_offset, log2_block_size),
                      HashRef(file_->metadata_block(), block.hash)});
      block_offset += size_t{1} << log2_block_size;
    }

    return refs;
  }

  void LoadDataBlock(uint32_t block_number, uint32_t data_size, Block::HashRef data_hash) {
    // Resize detaches stale cached blocks; reusing them would route later writes into an object that can no longer
    // flush to disk.
    if (current_data_block && !current_data_block->detached() &&
        file_->quota()->to_area_block_number(current_data_block->physical_block_number()) == block_number)
      return;
    auto block = file_->quota()->LoadDataBlock(block_number, static_cast<BlockSize>(file_->quota()->block_size_log2()),
                                               GetDataBlockType(), data_size, std::move(data_hash),
                                               !(file_->metadata()->flags.value() & EntryMetadata::UNENCRYPTED_FILE));
    if (!block.has_value())
      throw WfsException(WfsError::kFileDataCorrupted);
    current_data_block = std::move(*block);
  }
};

// Category 1 - File data in regluar blocks, in the attribute metadata there is a reversed list of block numbers and
// hashes. Limited to 5 blocks. (no minumum)
class File::BlocksLayoutAccessor : public File::BlockListLayoutAccessor {
 public:
  BlocksLayoutAccessor(const std::shared_ptr<File>& file) : BlockListLayoutAccessor(file) {}

 protected:
  BlockType GetDataBlockType() const override { return BlockType::Single; }
};

// Category 2 - File data in large block (8 regular blocks), in the attribute metadata there is a reversed list of block
// numbers and hashes. Limited to 5 large blocks. (minimum size of more than 1 regular block)
class File::LargeBlocksLayoutAccessor : public File::BlockListLayoutAccessor {
 public:
  LargeBlocksLayoutAccessor(const std::shared_ptr<File>& file) : BlockListLayoutAccessor(file) {}

 protected:
  BlockType GetDataBlockType() const override { return BlockType::Large; }
};

// Category 3 - File data in clusters of large block (8 large blocksblocks), in the attribute metadata there is a
// reversed list of block number and 8 hashes for each cluster. Limited to 4 clusters. (minimum size of more than 1
// large block)
class File::ClustersLayoutAccessor : public File::LargeBlocksLayoutAccessor {
 public:
  ClustersLayoutAccessor(const std::shared_ptr<File>& file) : LargeBlocksLayoutAccessor(file) {}

  size_t GetMetadataSize() const override { return GetMetadataItemsCount() * sizeof(DataBlocksClusterMetadata); }

  DataRef GetDataRef(size_t offset, size_t size, size_t file_size) override {
    return GetDataRefFromClustersList(/*cluster_list_start=*/0, offset, size, file_size, file_->metadata_block(),
                                      ClusterRefs());
  }

  std::vector<DataBlockRef> EnumerateBlocks() const override {
    return EnumerateClusterDataBlockRefs(/*cluster_list_start=*/0, file_->metadata_block(), ClusterRefs(),
                                         file_->metadata()->size_on_disk.value());
  }

 protected:
  template <typename ClusterArray>
  DataRef GetDataRefFromClustersList(size_t cluster_list_start,
                                     size_t offset,
                                     size_t size,
                                     size_t file_size,
                                     const std::shared_ptr<Block>& metadata_block,
                                     ClusterArray&& clusters_list) {
    auto offset_in_cluster_list = offset - (cluster_list_start << ClusterDataLog2Size());
    auto block_position = BlockPositionForOffset(offset_in_cluster_list, GetDataBlockSize());
    auto block_offset = floor_pow2(offset, GetDataBlockSize());
    auto location = FileDataBlockLocationForLogicalMetadata<FileLayoutCategory::Clusters>(metadata_block, clusters_list,
                                                                                          block_position.index);
    return GetDataFromBlock({location.block_number, location.block_type, block_offset,
                             DataSizeForBlock(file_size, block_offset, GetDataBlockSize()), std::move(location.hash)},
                            block_position.offset_in_block, size);
  }

  template <typename ClusterArray>
  std::vector<DataBlockRef> EnumerateClusterDataBlockRefs(size_t cluster_list_start,
                                                          const std::shared_ptr<Block>& metadata_block,
                                                          ClusterArray&& clusters_list,
                                                          size_t size) const {
    std::vector<DataBlockRef> refs;
    refs.reserve(clusters_list.size() * (size_t{1} << log2_size(BlockType::Cluster) >> log2_size(GetDataBlockType())));

    size_t block_offset = cluster_list_start << ClusterDataLog2Size();
    const auto data_blocks_count =
        std::ranges::size(clusters_list) * FileDataUnitLayoutTraits<FileLayoutCategory::Clusters>::kDataBlocksPerUnit;
    for (size_t data_block_index = 0; data_block_index < data_blocks_count && block_offset < size; ++data_block_index) {
      auto location = FileDataBlockLocationForLogicalMetadata<FileLayoutCategory::Clusters>(
          metadata_block, clusters_list, data_block_index);
      refs.push_back({location.block_number, location.block_type, block_offset,
                      DataSizeForBlock(size, block_offset, GetDataBlockSize()), std::move(location.hash)});
      block_offset += size_t{1} << GetDataBlockSize();
    }

    return refs;
  }

  size_t ClusterDataLog2Size() const { return LayoutAccessor::ClusterDataLog2Size(); }
};

// Category 4 - File data in clusters of large block (8 large blocksblocks), in the attribute metadata there is list of
// block numbers of metadata block with lists of block number and 8 hashes for each cluster. Limited to 237 metadata
// blocks of lists. (max file size) (minumum size of more/equal than 1 cluster)
class File::ClusterMetadataBlocksLayoutAccessor : public File::ClustersLayoutAccessor {
 public:
  ClusterMetadataBlocksLayoutAccessor(const std::shared_ptr<File>& file) : ClustersLayoutAccessor(file) {}

  size_t GetMetadataSize() const override { return GetMetadataItemsCount() * sizeof(uint32_be_t); }

  DataRef GetDataRef(size_t offset, size_t size, size_t file_size) override {
    auto blocks_list = ClusterMetadataBlockRefs();
    auto cluster_index = offset >> ClusterDataLog2Size();
    int64_t block_index = cluster_index / ClustersInBlock();
    LoadMetadataBlock(blocks_list[block_index].value());
    return GetDataRefFromClustersList(
        block_index * ClustersInBlock(), offset, size, file_size, current_metadata_block,
        std::span<const DataBlocksClusterMetadata>{
            current_metadata_block->get_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader)),
            ClustersInBlock()});
  }

  std::vector<DataBlockRef> EnumerateBlocks() const override {
    std::vector<DataBlockRef> refs;
    auto blocks_list = ClusterMetadataBlockRefs();
    size_t cluster_list_start = 0;
    for (const auto& block_number : blocks_list) {
      if (cluster_list_start << ClusterDataLog2Size() >= file_->metadata()->size_on_disk.value())
        break;
      const auto metadata_block = throw_if_error(file_->quota()->LoadMetadataBlock(block_number.value()));
      auto block_refs = EnumerateClusterDataBlockRefs(
          cluster_list_start, metadata_block,
          std::span<const DataBlocksClusterMetadata>{
              metadata_block->get_object<DataBlocksClusterMetadata>(sizeof(MetadataBlockHeader)), ClustersInBlock()},
          file_->metadata()->size_on_disk.value());
      refs.insert(refs.end(), block_refs.begin(), block_refs.end());
      cluster_list_start += ClustersInBlock();
    }
    return refs;
  }

  void FreeOwnedBlocks() override {
    ClustersLayoutAccessor::FreeOwnedBlocks();
    for (const auto& block_number : ClusterMetadataBlockRefs()) {
      if (!file_->quota()->DeleteBlocks(block_number.value(), 1))
        throw WfsException(WfsError::kFreeBlocksAllocatorCorrupted);
    }
  }

 protected:
  std::shared_ptr<Block> current_metadata_block;

  void LoadMetadataBlock(uint32_t block_number) {
    if (current_metadata_block &&
        file_->quota()->to_area_block_number(current_metadata_block->physical_block_number()) == block_number)
      return;
    auto metadata_block = file_->quota()->LoadMetadataBlock(block_number);
    if (!metadata_block.has_value())
      throw WfsException(WfsError::kFileMetadataCorrupted);
    current_metadata_block = std::move(*metadata_block);
  }

  size_t ClustersInBlock() const { return ClustersPerMetadataBlock(); }
};

std::shared_ptr<File::LayoutAccessor> File::CreateLayoutAccessor(std::shared_ptr<File> file) {
  switch (FileLayout::CategoryFromValue(file->metadata()->size_category.value())) {
    case FileLayoutCategory::Inline:
      return std::make_shared<InlineLayoutAccessor>(file);
    case FileLayoutCategory::Blocks:
      return std::make_shared<BlocksLayoutAccessor>(file);
    case FileLayoutCategory::LargeBlocks:
      return std::make_shared<LargeBlocksLayoutAccessor>(file);
    case FileLayoutCategory::Clusters:
      return std::make_shared<ClustersLayoutAccessor>(file);
    case FileLayoutCategory::ClusterMetadataBlocks:
      return std::make_shared<ClusterMetadataBlocksLayoutAccessor>(file);
  }
  throw std::runtime_error("Unexpected file category");  // TODO: Change to WfsError
}
