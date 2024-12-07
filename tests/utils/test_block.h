/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "block.h"

class TestBlock : public Block {
 public:
  TestBlock(std::shared_ptr<BlocksDevice> device, uint32_t physical_block_number);
  ~TestBlock() override;

  static std::shared_ptr<TestBlock> LoadMetadataBlock(std::shared_ptr<BlocksDevice> device,
                                                      uint32_t physical_block_number,
                                                      bool new_block = true);
};
