/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "area.h"

class TransactionsArea : public Area {
 public:
  TransactionsArea(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block);

  static std::expected<std::shared_ptr<TransactionsArea>, WfsError> Create(std::shared_ptr<WfsDevice> wfs_device,
                                                                           std::shared_ptr<Area> parent_area,
                                                                           uint32_t device_block_number,
                                                                           uint32_t device_blocks_count);

 private:
  void Init(std::shared_ptr<Area> parent_area, uint32_t device_blocks_count);
};
