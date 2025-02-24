/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "test_area.h"

#include <algorithm>

TestArea::TestArea(std::shared_ptr<Block> block) : Area(nullptr, std::move(block)) {}

void TestArea::Init(uint32_t blocks_count) {
  auto* header = mutable_header();
  std::fill(reinterpret_cast<std::byte*>(header), reinterpret_cast<std::byte*>(header + 1), std::byte{0});
  header->blocks_count = blocks_count;
  header->block_size_log2 = static_cast<uint8_t>(BlockSize::Logical);
}
