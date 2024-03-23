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

class BlocksDevice;
class Directory;
class FreeBlocksAllocator;

class Area : public std::enable_shared_from_this<Area> {
 public:
  Area(const std::shared_ptr<BlocksDevice>& device,
       const std::shared_ptr<Area>& root_area,
       const std::shared_ptr<MetadataBlock>& block,
       const std::string& root_directory_name,
       const AttributesBlock& root_directory_attributes);

  static std::expected<std::shared_ptr<Area>, WfsError> LoadRootArea(const std::shared_ptr<BlocksDevice>& device);

  std::expected<std::shared_ptr<Area>, WfsError> GetArea(uint32_t block_number,
                                                         const std::string& root_directory_name,
                                                         const AttributesBlock& root_directory_attributes,
                                                         Block::BlockSize size);

  std::expected<std::shared_ptr<Directory>, WfsError> GetRootDirectory();

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

  uint32_t RelativeBlockNumber(uint32_t block_number) const;
  uint32_t AbsoluteBlockNumber(uint32_t block_number) const;

  size_t GetDataBlockLog2Size() const;

  uint32_t BlockNumber() const;
  uint32_t BlocksCount() const;

  size_t BlocksCacheSize() const;

  std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> GetFreeBlocksAllocator();

 private:
  friend class Wfs;
  friend class Recovery;
  friend class FreeBlocksAllocator;

  static constexpr uint32_t FreeBlocksAllocatorBlockNumber = 1;

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

  uint32_t ToBasicBlockNumber(uint32_t block_number) const;
  uint32_t IV(uint32_t block_number) const;

  std::shared_ptr<BlocksDevice> device_;
  std::shared_ptr<Area> root_area_;  // Empty pointer for root area

  std::shared_ptr<MetadataBlock> header_block_;

  std::string root_directory_name_;
  AttributesBlock root_directory_attributes_;
};
