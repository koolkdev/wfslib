/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs_device.h"

#include <filesystem>
#include <random>

#include "area.h"
#include "blocks_device.h"
#include "device.h"
#include "directory.h"
#include "quota_area.h"
#include "structs.h"
#include "transactions_area.h"

WfsDevice::WfsDevice(std::shared_ptr<BlocksDevice> device, std::shared_ptr<Block> root_block)
    : device_(std::move(device)), root_block_(std::move(root_block)) {}

WfsDevice::~WfsDevice() {
  Flush();
}

std::shared_ptr<Entry> WfsDevice::GetEntry(std::string_view filename) {
  if (filename == "/")
    return GetDirectory("/");
  std::filesystem::path path(filename);
  auto dir = GetDirectory(path.parent_path().string());
  if (!dir)
    return nullptr;
  auto entry = dir->GetEntry(path.filename().string());
  if (!entry.has_value()) {
    if (entry.error() == WfsError::kEntryNotFound)
      return nullptr;
    else
      throw WfsException(entry.error());
  }
  return *entry;
}

std::shared_ptr<File> WfsDevice::GetFile(std::string_view filename) {
  std::filesystem::path path(filename);
  auto dir = GetDirectory(path.parent_path().string());
  if (!dir)
    return nullptr;
  auto file = dir->GetFile(path.filename().string());
  if (!file.has_value()) {
    if (file.error() == WfsError::kEntryNotFound)
      return nullptr;
    else
      throw WfsException(file.error());
  }
  return *file;
}

std::shared_ptr<Directory> WfsDevice::GetDirectory(std::string_view filename) {
  std::filesystem::path path(filename);
  auto current_directory = GetRootDirectory();
  if (!current_directory.has_value()) {
    if (current_directory.error() == WfsError::kEntryNotFound)
      return nullptr;
    else
      throw WfsException(current_directory.error());
  }

  for (const auto& part : path) {
    // the first part is "/"
    if (part == "/")
      continue;
    current_directory = (*current_directory)->GetDirectory(part.string());
    if (!current_directory.has_value()) {
      if (current_directory.error() == WfsError::kEntryNotFound)
        return nullptr;
      else
        throw WfsException(current_directory.error());
    }
  }
  return *current_directory;
}

void WfsDevice::Flush() {
  device_->FlushAll();
}

std::shared_ptr<QuotaArea> WfsDevice::GetRootArea() {
  return std::make_shared<QuotaArea>(shared_from_this(), root_block_);
}

std::expected<std::shared_ptr<Directory>, WfsError> WfsDevice::GetRootDirectory() {
  return GetRootArea()->LoadRootDirectory("", {root_block_, root_block_->to_offset(&header()->root_quota_metadata)});
}

// static
std::expected<std::shared_ptr<WfsDevice>, WfsError> WfsDevice::Open(std::shared_ptr<Device> device,
                                                                    std::optional<std::vector<std::byte>> key) {
  return Open(std::make_shared<BlocksDevice>(std::move(device), std::move(key)));
}

// static
std::expected<std::shared_ptr<WfsDevice>, WfsError> WfsDevice::Open(std::shared_ptr<BlocksDevice> device) {
  auto block = Block::LoadMetadataBlock(device, /*dvice_block_number=*/0, BlockSize::Physical, /*iv=*/0);
  if (!block.has_value()) {
    block = Block::LoadMetadataBlock(device, /*dvice_block_number=*/0, BlockSize::Logical, /*iv=*/0);
    if (!block.has_value())
      return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }
  auto* header = (*block)->get_object<WfsDeviceHeader>(header_offset());
  if (header->version.value() != WFS_VERSION)
    return std::unexpected(WfsError::kInvalidWfsVersion);
  return std::make_shared<WfsDevice>(std::move(device), std::move(*block));
}

// static
std::expected<std::shared_ptr<WfsDevice>, WfsError> WfsDevice::Create(std::shared_ptr<Device> device,
                                                                      std::optional<std::vector<std::byte>> key) {
  return Create(std::make_shared<BlocksDevice>(std::move(device), std::move(key)));
}

// static
std::expected<std::shared_ptr<WfsDevice>, WfsError> WfsDevice::Create(std::shared_ptr<BlocksDevice> device) {
  auto block = Block::LoadMetadataBlock(device, /*dvice_block_number=*/0, BlockSize::Logical, /*iv=*/0,
                                        /*load_data=*/false);
  if (!block.has_value()) {
    return std::unexpected(block.error());
  }

  auto wfs_device = std::make_shared<WfsDevice>(std::move(device), std::move(*block));
  wfs_device->Init();
  return wfs_device;
}

std::expected<std::shared_ptr<Block>, WfsError> WfsDevice::LoadMetadataBlock(const Area* area,
                                                                             uint32_t physical_block_number,
                                                                             BlockSize block_size,
                                                                             bool new_block) const {
  return Block::LoadMetadataBlock(device_, physical_block_number, block_size, CalcIV(area, physical_block_number),
                                  !new_block);
}

std::expected<std::shared_ptr<Block>, WfsError> WfsDevice::LoadDataBlock(const Area* area,
                                                                         uint32_t physical_block_number,
                                                                         BlockSize block_size,
                                                                         BlockType block_type,
                                                                         uint32_t data_size,
                                                                         Block::HashRef data_hash,
                                                                         bool encrypted,
                                                                         bool new_block) const {
  return Block::LoadDataBlock(device_, physical_block_number, block_size, block_type, data_size,
                              CalcIV(area, physical_block_number), std::move(data_hash), encrypted, !new_block);
}

uint32_t WfsDevice::CalcIV(const Area* area, uint32_t physical_block_number) const {
  return (area->header()->iv.value() ^ header()->iv.value()) +
         ((physical_block_number - area->physical_block_number())
          << (log2_size(BlockSize::Physical) - device_->device()->Log2SectorSize()));
}

std::expected<std::shared_ptr<TransactionsArea>, WfsError> WfsDevice::GetTransactionsArea(bool backup_area) {
  auto root_area = GetRootArea();
  auto block = LoadMetadataBlock(
      root_area.get(), header()->transactions_area_block_number.value() + (backup_area ? 1 : 0), BlockSize::Physical);
  if (!block.has_value())
    return std::unexpected(WfsError::kTransactionsAreaCorrupted);
  return std::make_shared<TransactionsArea>(shared_from_this(), std::move(*block));
}

void WfsDevice::Init() {
  constexpr uint32_t kTransactionsAreaEnd = 0x1000;

  uint32_t blocks_count =
      device_->device()->SectorsCount() >> (log2_size(BlockSize::Logical) - device_->device()->Log2SectorSize());

  std::random_device rand_device;
  std::default_random_engine rand_engine{rand_device()};
  std::uniform_int_distribution<uint32_t> random_iv_generator(std::numeric_limits<uint32_t>::min(),
                                                              std::numeric_limits<uint32_t>::max());

  // Initialize device header
  auto* header = mutable_header();
  std::fill(reinterpret_cast<std::byte*>(header), reinterpret_cast<std::byte*>(header + 1), std::byte{0});
  header->iv = random_iv_generator(rand_engine);
  header->device_type = static_cast<uint16_t>(DeviceType::USB);  // TODO
  header->version = WFS_VERSION;
  header->root_quota_metadata.flags =
      EntryMetadata::DIRECTORY | EntryMetadata::AREA_SIZE_REGULAR | EntryMetadata::QUOTA;
  header->root_quota_metadata.quota_blocks_count = blocks_count;
  header->transactions_area_block_number = QuotaArea::kReservedAreaBlocks
                                           << (log2_size(BlockSize::Logical) - log2_size(BlockSize::Physical));
  header->transactions_area_blocks_count = kTransactionsAreaEnd - header->transactions_area_block_number.value();

  // Initialize root area
  auto root_area =
      throw_if_error(QuotaArea::Create(shared_from_this(), /*parent_area=*/nullptr,
                                       blocks_count >> (log2_size(BlockSize::Logical) - log2_size(BlockSize::Physical)),
                                       BlockSize::Logical, {{0, blocks_count}}));

  auto transactions_area = throw_if_error(TransactionsArea::Create(shared_from_this(), root_area,
                                                                   header->transactions_area_block_number.value(),
                                                                   header->transactions_area_blocks_count.value()));
}
