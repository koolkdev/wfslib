/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "block.h"
#include "structs.h"

class WfsDevice;
class Directory;
class FreeBlocksAllocator;
struct AttributesRef;

class Area : public std::enable_shared_from_this<Area> {
 public:
  Area(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block);

  // static std::expected<std::shared_ptr<Area>, WfsError> CreateRootArea(const std::shared_ptr<BlocksDevice>& device);

  std::expected<std::shared_ptr<Area>, WfsError> GetArea(uint32_t area_block_number, Block::BlockSize size);

  std::expected<std::shared_ptr<Directory>, WfsError> LoadRootDirectory(std::string name, AttributesRef attributes);
  std::expected<std::shared_ptr<Directory>, WfsError> GetShadowDirectory1();
  std::expected<std::shared_ptr<Directory>, WfsError> GetShadowDirectory2();

  std::expected<std::shared_ptr<Directory>, WfsError> LoadDirectory(uint32_t area_block_number,
                                                                    std::string name,
                                                                    AttributesRef attributes);

  std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(uint32_t area_block_number,
                                                                    bool new_block = false) const;
  std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(uint32_t area_block_number,
                                                                    Block::BlockSize size,
                                                                    bool new_block = false) const;
  std::expected<std::shared_ptr<Block>, WfsError> LoadDataBlock(uint32_t area_block_number,
                                                                Block::BlockSize size,
                                                                uint32_t data_size,
                                                                Block::HashRef data_hash,
                                                                bool encrypted,
                                                                bool new_block = false) const;

  std::expected<std::shared_ptr<Block>, WfsError> AllocMetadataBlock();
  std::expected<std::vector<uint32_t>, WfsError> AllocDataBlocks(uint32_t chunks_count,
                                                                 Block::BlockSizeType chunk_size);
  bool DeleteBlocks(uint32_t area_block_number, uint32_t area_blocks_count);

  uint32_t to_area_block_number(uint32_t device_block_number) const {
    return to_area_blocks_count(device_block_number - header_block_->device_block_number());
  }

  uint32_t to_device_block_number(uint32_t area_block_number) const {
    return header_block_->device_block_number() + to_device_blocks_count(area_block_number);
  }

  uint32_t to_area_blocks_count(uint32_t device_blocks_count) const {
    return device_blocks_count >> (block_size_log2() - Block::BlockSize::Basic);
  }

  uint32_t to_device_blocks_count(uint32_t area_blocks_count) const {
    return area_blocks_count << (block_size_log2() - Block::BlockSize::Basic);
  }

  size_t block_size_log2() const { return header()->block_size_log2.value(); }
  size_t block_size() const { return size_t{1} << block_size_log2(); }

  uint32_t device_block_number() const { return header_block_->device_block_number(); }

  // In area blocks count
  uint32_t blocks_count() const { return header()->blocks_count.value(); }

  uint32_t ReservedBlocksCount() const;

  std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> GetFreeBlocksAllocator();

 protected:
  auto* mutable_header() { return block()->get_mutable_object<WfsAreaHeader>(header_offset()); }
  const auto* header() const { return block()->get_object<WfsAreaHeader>(header_offset()); }

  Block* block() { return header_block_.get(); }
  const Block* block() const { return header_block_.get(); }

 private:
  friend class Recovery;
  friend class WfsDevice;

  static constexpr uint32_t kFreeBlocksAllocatorBlockNumber = 1;
  static constexpr uint32_t kFreeBlocksAllocatorInitialFTreeBlockNumber = 2;
  static constexpr uint32_t kRootDirectoryBlockNumber = 3;
  static constexpr uint32_t kShadowDirectory1BlockNumber = 4;
  static constexpr uint32_t kShadowDirectory2BlockNumber = 5;
  static constexpr uint32_t kReservedAreaBlocks = 6;

  static constexpr uint32_t kTransactionsBlockNumber = 6;

  bool is_root_area() const { return device_block_number() == 0; }
  uint16_t header_offset() const {
    return sizeof(MetadataBlockHeader) + (is_root_area() ? sizeof(WfsDeviceHeader) : 0);
  }

  uint32_t IV(uint32_t block_number) const;

  std::shared_ptr<WfsDevice> wfs_device_;
  std::shared_ptr<Block> header_block_;
};
