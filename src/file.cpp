/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "block.h"
#include "file_layout.h"
#include "quota_area.h"
#include "structs.h"

uint32_t File::Size() const {
  return metadata()->file_size.value();
}

uint32_t File::SizeOnDisk() const {
  return metadata()->size_on_disk.value();
}

bool File::IsEncrypted() const {
  return !(metadata()->flags.value() & EntryMetadata::UNENCRYPTED_FILE);
}

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
    auto blocks_list = DataBlockRefs();
    auto block_position = BlockPositionForOffset(offset, GetDataBlockSize());
    auto block_ref =
        DataBlockRef{blocks_list[block_position.index].block_number.value(), GetDataBlockType(), block_position.offset,
                     DataSizeForBlock(file_size, block_position.offset, GetDataBlockSize()),
                     HashRef(file_->metadata_block(), blocks_list[block_position.index].hash)};
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
    if (current_data_block &&
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
    auto [cluster_index, offset_in_cluster] = div_pow2(offset_in_cluster_list, ClusterDataLog2Size());
    auto block_position = BlockPositionForOffset(offset_in_cluster, GetDataBlockSize());
    auto block_offset = floor_pow2(offset, GetDataBlockSize());
    return GetDataFromBlock(
        {clusters_list[cluster_index].block_number.value() +
             static_cast<uint32_t>(block_position.index << log2_size(GetDataBlockType())),
         GetDataBlockType(), block_offset, DataSizeForBlock(file_size, block_offset, GetDataBlockSize()),
         HashRef(metadata_block, clusters_list[cluster_index].hash[block_position.index])},
        block_position.offset_in_block, size);
  }

  template <typename ClusterArray>
  std::vector<DataBlockRef> EnumerateClusterDataBlockRefs(size_t cluster_list_start,
                                                          const std::shared_ptr<Block>& metadata_block,
                                                          ClusterArray&& clusters_list,
                                                          size_t size) const {
    std::vector<DataBlockRef> refs;
    refs.reserve(clusters_list.size() * (size_t{1} << log2_size(BlockType::Cluster) >> log2_size(GetDataBlockType())));

    size_t cluster_offset = cluster_list_start << ClusterDataLog2Size();
    for (const auto& cluster : clusters_list) {
      if (cluster_offset >= size)
        break;
      for (size_t block_index = 0; block_index < std::size(cluster.hash); ++block_index) {
        auto block_offset = cluster_offset + (block_index << GetDataBlockSize());
        if (block_offset >= size)
          break;
        refs.push_back(
            {cluster.block_number.value() + static_cast<uint32_t>(block_index << log2_size(GetDataBlockType())),
             GetDataBlockType(), block_offset, DataSizeForBlock(size, block_offset, GetDataBlockSize()),
             HashRef(metadata_block, cluster.hash[block_index])});
      }
      cluster_offset += size_t{1} << ClusterDataLog2Size();
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

void File::Resize(size_t new_size) {
  // TODO: implment it, write now change up to size_on_disk without ever chaning size_on_disk
  new_size = std::min(new_size, static_cast<size_t>(metadata_.get()->size_on_disk.value()));
  size_t old_size = metadata_.get()->file_size.value();
  if (new_size != old_size) {
    CreateLayoutAccessor(shared_from_this())->Resize(new_size);
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
