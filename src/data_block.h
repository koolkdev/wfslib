/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include "block.h"

class MetadataBlock;

class DataBlock : public Block {
 public:
  struct DataBlockHash {
    std::shared_ptr<MetadataBlock> block;
    size_t hash_offset;
  };

  DataBlock(const std::shared_ptr<DeviceEncryption>& device,
            uint32_t block_number,
            Block::BlockSize size_category,
            uint32_t data_size,
            uint32_t iv,
            const DataBlockHash& data_hash,
            bool encrypted);
  ~DataBlock() override;

  static std::shared_ptr<DataBlock> LoadBlock(const std::shared_ptr<DeviceEncryption>& device,
                                              uint32_t block_number,
                                              Block::BlockSize size_category,
                                              uint32_t data_size,
                                              uint32_t iv,
                                              const DataBlockHash& data_hash,
                                              bool encrypted);

 protected:
  std::span<std::byte> Hash() override;
  std::span<const std::byte> Hash() const override;

 private:
  std::shared_ptr<MetadataBlock> hash_metadata_block() { return data_hash_.block; }
  std::shared_ptr<const MetadataBlock> hash_metadata_block() const { return data_hash_.block; }
  size_t hash_offset() const { return data_hash_.hash_offset; }

  DataBlockHash data_hash_;
};
