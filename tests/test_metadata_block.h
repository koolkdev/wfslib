/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "../src/metadata_block.h"

class TestMetadataBlock : public MetadataBlock {
 public:
  TestMetadataBlock(std::shared_ptr<BlocksDevice> device, uint32_t block_number);
  ~TestMetadataBlock() override;

  static std::shared_ptr<TestMetadataBlock> LoadBlock(std::shared_ptr<BlocksDevice> device,
                                                      uint32_t block_number,
                                                      bool new_block = true);
};
