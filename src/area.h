/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>

#include "block.h"
#include "structs.h"

class WfsDevice;

class Area {
 public:
  Area(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block);

  // TODO Create transactions area

  std::expected<std::shared_ptr<Area>, WfsError> GetArea(uint32_t area_block_number, BlockSize size);

  std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(uint32_t area_block_number,
                                                                    bool new_block = false) const;
  std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(uint32_t area_block_number,
                                                                    BlockSize block_size,
                                                                    bool new_block = false) const;
  std::expected<std::shared_ptr<Block>, WfsError> LoadDataBlock(uint32_t area_block_number,
                                                                BlockSize block_size,
                                                                BlockType block_type,
                                                                uint32_t data_size,
                                                                Block::HashRef data_hash,
                                                                bool encrypted,
                                                                bool new_block = false) const;

  uint32_t to_area_block_number(uint32_t physical_block_number) const {
    return to_area_blocks_count(physical_block_number - header_block_->physical_block_number());
  }

  uint32_t to_physical_block_number(uint32_t area_block_number) const {
    return header_block_->physical_block_number() + to_physical_blocks_count(area_block_number);
  }

  uint32_t to_area_blocks_count(uint32_t physical_blocks_count) const {
    return physical_blocks_count >> (block_size_log2() - log2_size(BlockSize::Physical));
  }

  uint32_t to_physical_blocks_count(uint32_t area_blocks_count) const {
    return area_blocks_count << (block_size_log2() - log2_size(BlockSize::Physical));
  }

  size_t block_size_log2() const { return header()->block_size_log2.value(); }
  size_t block_size() const { return size_t{1} << block_size_log2(); }

  uint32_t physical_block_number() const { return header_block_->physical_block_number(); }

  // In area blocks count
  uint32_t blocks_count() const { return header()->blocks_count.value(); }

 protected:
  friend class Recovery;
  friend class WfsDevice;

  void Init(std::shared_ptr<Area> parent_area, uint32_t blocks_count, BlockSize block_size);

  const std::shared_ptr<Block>& header_block() const { return header_block_; };

  uint16_t header_offset() const {
    return sizeof(MetadataBlockHeader) + (is_root_area() ? sizeof(WfsDeviceHeader) : 0);
  }

  WfsAreaHeader* mutable_header() { return header_block_->get_mutable_object<WfsAreaHeader>(header_offset()); }
  const WfsAreaHeader* header() const { return header_block_->get_object<WfsAreaHeader>(header_offset()); }

  const std::shared_ptr<WfsDevice>& wfs_device() { return wfs_device_; }

  bool is_root_area() const { return physical_block_number() == 0; }

 private:
  std::shared_ptr<WfsDevice> wfs_device_;
  std::shared_ptr<Block> header_block_;
};
