/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <algorithm>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "block.h"
#include "utils/test_blocks_device.h"

namespace {
auto LoadBlock(std::shared_ptr<TestBlocksDevice> device, uint32_t block_number, uint32_t data_size) {
  return Block::LoadDataBlock(std::move(device), block_number, BlockSize::Logical, BlockType::Single, data_size,
                              /*iv=*/0, Block::HashRef{}, /*encrypted=*/false, /*load_data=*/false);
}
}  // namespace

TEST_CASE("Block resize updates used size and zero-fills growth") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto block_result = LoadBlock(device, /*block_number=*/7, /*data_size=*/600);
  REQUIRE(block_result.has_value());
  auto block = *block_result;
  std::ranges::fill(block->mutable_data(), std::byte{0x5a});

  block->Resize(700);

  CHECK(block->size() == 700);
  REQUIRE(block->data().size() == 700);
  CHECK(std::ranges::all_of(block->data().subspan(0, 600), [](std::byte value) { return value == std::byte{0x5a}; }));
  CHECK(std::ranges::all_of(block->data().subspan(600), [](std::byte value) { return value == std::byte{0}; }));
}

TEST_CASE("Block resize shrinks readable span") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto block_result = LoadBlock(device, /*block_number=*/8, /*data_size=*/700);
  REQUIRE(block_result.has_value());
  auto block = *block_result;
  std::ranges::fill(block->mutable_data(), std::byte{0x7b});

  block->Resize(512);

  CHECK(block->size() == 512);
  REQUIRE(block->data().size() == 512);
  CHECK(std::ranges::all_of(block->data(), [](std::byte value) { return value == std::byte{0x7b}; }));
}

TEST_CASE("Block resize marks same-aligned-size changes dirty") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto block_result = LoadBlock(device, /*block_number=*/9, /*data_size=*/600);
  REQUIRE(block_result.has_value());
  auto block = *block_result;
  std::ranges::fill(block->mutable_data(), std::byte{0x11});
  block->Flush();
  REQUIRE(device->blocks_.at(9).size() == 1024);

  std::ranges::fill(device->blocks_.at(9), std::byte{0xcc});
  block->Resize(700);
  block->Flush();

  const auto& flushed = device->blocks_.at(9);
  REQUIRE(flushed.size() == 1024);
  CHECK(flushed[0] == std::byte{0x11});
  CHECK(flushed[599] == std::byte{0x11});
  CHECK(flushed[600] == std::byte{0});
  CHECK(flushed[699] == std::byte{0});
  CHECK(flushed[700] == std::byte{0});
}

TEST_CASE("Block resize flushes smaller aligned size") {
  auto device = std::make_shared<TestBlocksDevice>();
  auto block_result = LoadBlock(device, /*block_number=*/10, /*data_size=*/700);
  REQUIRE(block_result.has_value());
  auto block = *block_result;
  std::ranges::fill(block->mutable_data(), std::byte{0x3c});
  block->Flush();
  REQUIRE(device->blocks_.at(10).size() == 1024);

  block->Resize(512);
  block->Flush();

  const auto& flushed = device->blocks_.at(10);
  REQUIRE(flushed.size() == 512);
  CHECK(std::ranges::all_of(flushed, [](std::byte value) { return value == std::byte{0x3c}; }));
}
