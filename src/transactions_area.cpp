/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "transactions_area.h"

#include "wfs_device.h"

TransactionsArea::TransactionsArea(std::shared_ptr<WfsDevice> wfs_device, std::shared_ptr<Block> header_block)
    : Area(std::move(wfs_device), std::move(header_block)) {}

// static
std::expected<std::shared_ptr<TransactionsArea>, WfsError> TransactionsArea::Create(
    std::shared_ptr<WfsDevice> wfs_device,
    std::shared_ptr<Area> parent_area,
    uint32_t device_block_number,
    uint32_t device_blocks_count) {
  auto block = wfs_device->LoadMetadataBlock(parent_area.get(), device_block_number, Block::BlockSize::Basic,
                                             /*new_block=*/true);
  if (!block.has_value())
    return std::unexpected(block.error());
  auto transactions = std::make_shared<TransactionsArea>(std::move(wfs_device), std::move(*block));
  transactions->Init(parent_area, device_blocks_count);
  return transactions;
}

void TransactionsArea::Init(std::shared_ptr<Area> parent_area, uint32_t blocks_count) {
  Area::Init(parent_area, blocks_count, Block::BlockSize::Basic);

  mutable_header()->area_type = static_cast<uint8_t>(WfsAreaHeader::AreaType::TransactionsArea);

  // Initializaing transactinos area header not implemented because it is wiped on mount on the console anyway.
}
