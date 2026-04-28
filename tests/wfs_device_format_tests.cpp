/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <wfslib/device.h>
#include <wfslib/directory.h>
#include <wfslib/wfs_device.h>

#include "free_blocks_allocator.h"
#include "quota_area.h"
#include "transactions_area.h"
#include "utils/test_fixtures.h"

TEST_CASE_METHOD(MetadataBlockFixture,
                 "freshly created WFS device has an initialized empty root directory",
                 "[wfs-device][format]") {
  auto wfs_device = WfsDevice::Create(test_device);
  REQUIRE(wfs_device.has_value());

  auto root = (*wfs_device)->GetRootDirectory();
  REQUIRE(root.has_value());
  CHECK((*root)->size() == 0);
  CHECK((*root)->begin() == (*root)->end());

  auto root_by_path = (*wfs_device)->GetDirectory("/");
  REQUIRE(root_by_path);
  CHECK(root_by_path->size() == 0);
  CHECK(root_by_path->begin() == root_by_path->end());
}

TEST_CASE_METHOD(MetadataBlockFixture,
                 "freshly created WFS device initializes shadow directories",
                 "[wfs-device][format]") {
  auto wfs_device = WfsDevice::Create(test_device);
  REQUIRE(wfs_device.has_value());

  auto root_area = (*wfs_device)->GetRootArea();

  auto shadow_1 = root_area->GetShadowDirectory1();
  REQUIRE(shadow_1.has_value());
  CHECK((*shadow_1)->size() == 0);
  CHECK((*shadow_1)->begin() == (*shadow_1)->end());

  auto shadow_2 = root_area->GetShadowDirectory2();
  REQUIRE(shadow_2.has_value());
  CHECK((*shadow_2)->size() == 0);
  CHECK((*shadow_2)->begin() == (*shadow_2)->end());
}

TEST_CASE_METHOD(MetadataBlockFixture,
                 "freshly created WFS device initializes root quota and transactions areas",
                 "[wfs-device][format]") {
  auto wfs_device = WfsDevice::Create(test_device);
  REQUIRE(wfs_device.has_value());

  auto root_area = (*wfs_device)->GetRootArea();
  const auto expected_root_area_blocks = test_device->device()->SectorsCount() >>
                                         (log2_size(BlockSize::Logical) - test_device->device()->Log2SectorSize() + 1);
  CHECK(root_area->block_size_log2() == log2_size(BlockSize::Logical));
  CHECK(root_area->blocks_count() == expected_root_area_blocks);

  auto transactions_area = (*wfs_device)->GetTransactionsArea();
  REQUIRE(transactions_area.has_value());
  CHECK((*transactions_area)->block_size_log2() == log2_size(BlockSize::Physical));
  CHECK((*transactions_area)->blocks_count() == 0x1000 - root_area->to_physical_blocks_count(6));
}

TEST_CASE_METHOD(MetadataBlockFixture,
                 "freshly created WFS device reserves format metadata and transaction blocks",
                 "[wfs-device][format]") {
  auto wfs_device = WfsDevice::Create(test_device);
  REQUIRE(wfs_device.has_value());

  auto root_area = (*wfs_device)->GetRootArea();
  auto transactions_area = (*wfs_device)->GetTransactionsArea();
  REQUIRE(transactions_area.has_value());

  constexpr uint32_t kReservedMetadataBlocks = 6;
  const auto reserved_blocks =
      kReservedMetadataBlocks + root_area->to_area_blocks_count((*transactions_area)->blocks_count());

  auto free_blocks_allocator = root_area->GetFreeBlocksAllocator();
  REQUIRE(free_blocks_allocator.has_value());
  CHECK((*free_blocks_allocator)->free_blocks_count() == root_area->blocks_count() - reserved_blocks);

  auto metadata_block = root_area->AllocMetadataBlock();
  REQUIRE(metadata_block.has_value());
  CHECK(root_area->to_area_block_number((*metadata_block)->physical_block_number()) >= reserved_blocks);
}
