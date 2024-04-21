/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "../src/area.h"

class TestArea : public Area {
 public:
  TestArea(std::shared_ptr<Block> block);

  void Init(uint32_t blocks_count);
};
