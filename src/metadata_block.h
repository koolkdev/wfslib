/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include "block.h"
#include "utils.h"

struct MetadataBlockHeader;

class MetadataBlock : public Block {
 public:
  class Adapter {
   public:
    Adapter(std::shared_ptr<MetadataBlock> block) : block_(std::move(block)) {}
    std::span<std::byte> data() { return block_->data(); }
    std::span<const std::byte> data() const { return as_const(block_.get())->data(); }

   private:
    std::shared_ptr<MetadataBlock> block_;
  };

  MetadataBlock(const std::shared_ptr<DeviceEncryption>& device,
                uint32_t block_number,
                Block::BlockSize size_category,
                uint32_t iv);
  ~MetadataBlock() override;

  static std::shared_ptr<MetadataBlock> LoadBlock(const std::shared_ptr<DeviceEncryption>& device,
                                                  uint32_t block_number,
                                                  Block::BlockSize size_category,
                                                  uint32_t iv,
                                                  bool check_hash = true);

  MetadataBlockHeader* Header();
  const MetadataBlockHeader* Header() const;

  void Resize(uint32_t data_size) override;

 protected:
  std::span<std::byte> Hash() override;
  std::span<const std::byte> Hash() const override;
};
