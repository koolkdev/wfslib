/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include "block.h"

struct MetadataBlockHeader;

class MetadataBlock : public Block {
 public:
  class Adapter {
   public:
    Adapter(std::shared_ptr<MetadataBlock> block) : block_(std::move(block)) {}
    std::span<std::byte> data() { return block_->Data(); }

   private:
    std::shared_ptr<MetadataBlock> block_;
  };

  MetadataBlock(const std::shared_ptr<DeviceEncryption>& device,
                uint32_t block_number,
                Block::BlockSize size_category,
                uint32_t iv);

  static std::shared_ptr<MetadataBlock> LoadBlock(const std::shared_ptr<DeviceEncryption>& device,
                                                  uint32_t block_number,
                                                  Block::BlockSize size_category,
                                                  uint32_t iv,
                                                  bool check_hash = true);

  MetadataBlockHeader* Header();

 protected:
  std::span<std::byte> Hash() override;
};
