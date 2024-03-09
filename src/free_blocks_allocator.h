/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>

class EPTree;
class Area;
class MetadataBlock;

class FreeBlocksAllocator {
 public:
  FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block);

  uint32_t PopFreeBlockNumberFromMetadataCache();
  uint32_t FindFreeMetadataBlockNumber(uint32_t near);

 private:
  std::unique_ptr<EPTree> eptree_;
};
