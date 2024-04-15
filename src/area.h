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
#include "data_block.h"
#include "metadata_block.h"
#include "structs.h"
#include "wfs_item.h"

class Wfs;
class Directory;
class FreeBlocksAllocator;

class Area : public std::enable_shared_from_this<Area> {
 public:
  Area(std::shared_ptr<Wfs> wfs_device, std::shared_ptr<MetadataBlock> header_block);

  // static std::expected<std::shared_ptr<Area>, WfsError> CreateRootArea(const std::shared_ptr<BlocksDevice>& device);

  std::expected<std::shared_ptr<Area>, WfsError> GetArea(uint32_t block_number, Block::BlockSize size);

  std::expected<std::shared_ptr<Directory>, WfsError> GetRootDirectory(const std::string& name,
                                                                       const AttributesBlock& attributes);
  std::expected<std::shared_ptr<Directory>, WfsError> GetShadowDirectory1();
  std::expected<std::shared_ptr<Directory>, WfsError> GetShadowDirectory2();
  std::expected<std::shared_ptr<Area>, WfsError> GetTransactionsArea1() const;
  std::expected<std::shared_ptr<Area>, WfsError> GetTransactionsArea2() const;

  std::expected<std::shared_ptr<Directory>, WfsError> GetDirectory(uint32_t block_number,
                                                                   const std::string& name,
                                                                   const AttributesBlock& attributes);

  std::expected<std::shared_ptr<MetadataBlock>, WfsError> GetMetadataBlock(uint32_t block_number,
                                                                           bool new_block = false) const;
  std::expected<std::shared_ptr<MetadataBlock>, WfsError> GetMetadataBlock(uint32_t block_number,
                                                                           Block::BlockSize size,
                                                                           bool new_block = false) const;

  std::expected<std::shared_ptr<DataBlock>, WfsError> GetDataBlock(uint32_t block_number,
                                                                   Block::BlockSize size,
                                                                   uint32_t data_size,
                                                                   const DataBlock::DataBlockHash& data_hash,
                                                                   bool encrypted) const;

  std::expected<std::shared_ptr<MetadataBlock>, WfsError> AllocMetadataBlock();
  std::expected<std::vector<uint32_t>, WfsError> AllocDataBlocks(uint32_t chunks_count,
                                                                 Block::BlockSizeType chunk_size);
  bool DeleteBlocks(uint32_t block_number, uint32_t blocks_count);

  uint32_t ToRelativeBlockNumber(uint32_t absolute_block_number) const;
  uint32_t ToAbsoluteBlockNumber(uint32_t relative_block_number) const;
  uint32_t ToRelativeBlocksCount(uint32_t absolute_blocks_count) const;
  uint32_t ToAbsoluteBlocksCount(uint32_t relative_blocks_count) const;

  size_t BlockSizeLog2() const;

  uint32_t BlockNumber() const;
  uint32_t BlocksCount() const;

  size_t BlocksCacheSizeLog2() const;
  uint32_t ReservedBlocksCount() const;

  std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> GetFreeBlocksAllocator();

 private:
  friend class Wfs;
  friend class Recovery;
  friend class FreeBlocksAllocator;

  static constexpr uint32_t kFreeBlocksAllocatorBlockNumber = 1;
  static constexpr uint32_t kFreeBlocksAllocatorInitialFTreeBlockNumber = 2;
  static constexpr uint32_t kRootDirectoryBlockNumber = 3;
  static constexpr uint32_t kShadowDirectory1BlockNumber = 4;
  static constexpr uint32_t kShadowDirectory2BlockNumber = 5;
  static constexpr uint32_t kReservedAreaBlocks = 6;

  static constexpr uint32_t kTransactionsBlockNumber = 6;

  MetadataBlock* block() { return header_block_.get(); }
  const MetadataBlock* block() const { return header_block_.get(); }

  bool has_wfs_header() const { return BlockNumber() == 0; }
  uint16_t wfs_header_offset() const { return sizeof(MetadataBlockHeader); }
  uint16_t header_offset() const { return sizeof(MetadataBlockHeader) + (has_wfs_header() ? sizeof(WfsHeader) : 0); }

  WfsAreaHeader* mutable_header() { return block()->get_mutable_object<WfsAreaHeader>(header_offset()); }
  const WfsAreaHeader* header() const { return block()->get_object<WfsAreaHeader>(header_offset()); }

  WfsHeader* mutable_wfs_header() {
    return has_wfs_header() ? block()->get_mutable_object<WfsHeader>(wfs_header_offset()) : NULL;
  }
  const WfsHeader* wfs_header() const {
    return has_wfs_header() ? block()->get_object<WfsHeader>(wfs_header_offset()) : NULL;
  }

  uint32_t IV(uint32_t block_number) const;

  std::shared_ptr<Wfs> wfs_device_;
  std::shared_ptr<MetadataBlock> header_block_;
};
