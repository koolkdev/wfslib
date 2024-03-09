/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <expected>
#include <memory>

#include "block.h"
#include "errors.h"
#include "utils.h"

struct MetadataBlockHeader;

class MetadataBlock : public Block {
 public:
  MetadataBlock(const std::shared_ptr<BlocksDevice>& device,
                uint32_t block_number,
                Block::BlockSize size_category,
                uint32_t iv);
  ~MetadataBlock() override;

  static std::expected<std::shared_ptr<MetadataBlock>, WfsError> LoadBlock(const std::shared_ptr<BlocksDevice>& device,
                                                                           uint32_t block_number,
                                                                           Block::BlockSize size_category,
                                                                           uint32_t iv,
                                                                           bool check_hash = true,
                                                                           bool load_data = true);

  MetadataBlockHeader* Header();
  const MetadataBlockHeader* Header() const;

  void Resize(uint32_t data_size) override;

 protected:
  std::span<std::byte> MutableHash() override;
  std::span<const std::byte> Hash() const override;
};
