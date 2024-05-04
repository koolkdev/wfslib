/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <expected>

#include "area.h"
#include "errors.h"
#include "wfs_item.h"

class Directory;
class FreeBlocksAllocator;

class QuotaArea : public Area, public std::enable_shared_from_this<QuotaArea> {
 public:
  struct QuotaFragment {
    uint32_t block_number;
    uint32_t blocks_count;
  };

  QuotaArea(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block);

  // If parent_area null it is root area
  static std::expected<std::shared_ptr<QuotaArea>, WfsError> Create(std::shared_ptr<WfsDevice> wfs_device,
                                                                    std::shared_ptr<Area> parent_area,
                                                                    uint32_t blocks_count,
                                                                    Block::BlockSize block_size,
                                                                    const std::vector<QuotaFragment>& fragments);

  std::expected<std::shared_ptr<QuotaArea>, WfsError> LoadQuotaArea(uint32_t area_block_number, Block::BlockSize size);

  std::expected<std::shared_ptr<Directory>, WfsError> LoadRootDirectory(std::string name, AttributesRef attributes);
  std::expected<std::shared_ptr<Directory>, WfsError> GetShadowDirectory1();
  std::expected<std::shared_ptr<Directory>, WfsError> GetShadowDirectory2();

  std::expected<std::shared_ptr<Directory>, WfsError> LoadDirectory(uint32_t area_block_number,
                                                                    std::string name,
                                                                    AttributesRef attributes);

  std::expected<std::shared_ptr<Block>, WfsError> AllocMetadataBlock();
  std::expected<std::vector<uint32_t>, WfsError> AllocDataBlocks(uint32_t chunks_count,
                                                                 Block::BlockSizeType chunk_size);
  std::expected<std::vector<QuotaFragment>, WfsError> AllocAreaBlocks(uint32_t blocks_count);
  bool DeleteBlocks(uint32_t area_block_number, uint32_t area_blocks_count);

  // TODO: Private
  std::expected<std::shared_ptr<FreeBlocksAllocator>, WfsError> GetFreeBlocksAllocator();

 private:
  friend class Recovery;
  friend class WfsDevice;
  friend class TestArea;

  enum ReservedBlocks : uint32_t {
    kAreaHader = 0,
    kFreeBlocksAllocatorBlockNumber = 1,
    kFreeBlocksAllocatorInitialFTreeBlockNumber = 2,
    kRootDirectoryBlockNumber = 3,
    kShadowDirectory1BlockNumber = 4,
    kShadowDirectory2BlockNumber = 5,
    kReservedAreaBlocks = 6,
  };

  uint16_t quota_header_offset() const { return header_offset() + sizeof(*header()); };
  auto* mutable_quota_header() { return header_block()->get_mutable_object<WfsQuotaAreaHeader>(quota_header_offset()); }
  const auto* quota_header() const { return header_block()->get_object<WfsQuotaAreaHeader>(quota_header_offset()); }

  void Init(std::shared_ptr<Area> parent_area,
            uint32_t blocks_count,
            Block::BlockSize block_size,
            const std::vector<QuotaFragment>& fragments);
};
