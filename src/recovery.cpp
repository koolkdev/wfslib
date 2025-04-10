/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "recovery.h"

#include <array>

#include "area.h"
#include "blocks_device.h"
#include "device_encryption.h"
#include "directory.h"
#include "device.h"
#include "quota_area.h"
#include "structs.h"
#include "wfs_device.h"

namespace {

bool RestoreMetadataBlockIVParameters(std::shared_ptr<Device> device,
                                      std::shared_ptr<BlocksDevice> blocks_device,
                                      uint32_t block_number,
                                      uint32_t area_start_block_number,
                                      BlockSize block_size,
                                      uint32_t& area_xor_wfs_iv) {
  // The IV of the encryption of block looks like that:
  // DWORD[0]: block offset in disk in bytes
  // DWORD[1]: block area ^ device wfs 32 bit IVs ^ block number (the first one is in the area header, the second one in
  // the wfs header, the block number is in basic size which is 0x1000 bytes per block)
  // DWORD[2]: disk sectors count (LBAs available for WFS)
  // DWORD[3]: disk sector size in bytes (LBA size)
  // Without prior information DWORD[0] is known, DWORD[1] is unknown. DWORD[2]/[3] can be determined from the device
  // itself, but using the following methodolgy, we can restore the three of them anyway.
  // The encryption is CBC mode, so IV is xored with the data AFTER decryption of the first block, so if we can predict
  // the data of the first block, we can restore the IV. The reason it is possible is that all of those dwords fall on
  // bytes 4-16 while in metadata blocks the hash will in bytes 4-24. So if we decrypt the block with the right key, we
  // can get the right hash, and than we can xor the expected value with the value that we got after decryption, and we
  // will get the xor of our IV with the real.
  // The sector size will usually be 512 bytes anyway, so lets start with that.
  device->SetLog2SectorSize(9);
  // Set the sectors count to be enough to read our block
  device->SetSectorsCount((block_number << (log2_size(BlockSize::Physical) - 9)) + (1 << (log2_size(block_size) - 9)));
  // This is the initial iv that the encryption will use
  std::array<uint32_t, 4> iv = {block_number << log2_size(BlockSize::Physical), 0, device->SectorsCount(),
                                device->SectorSize()};
  auto block = *Block::LoadMetadataBlock(blocks_device, block_number, block_size, /*iv=*/0, /*load_data=*/true,
                                         /*check_hash=*/false);
  // The two last dwords of the IV is the sectors count and sector size, right now it is xored with our fake sector size
  // and sector count, and with the hash
  std::vector<std::byte> data{block->data().begin(), block->data().end()};
  auto first_4_dwords = reinterpret_cast<uint32_be_t*>(data.data());
  iv[0] ^= first_4_dwords[0].value();
  iv[1] ^= first_4_dwords[1].value();
  iv[2] ^= first_4_dwords[2].value();
  iv[3] ^= first_4_dwords[3].value();
  // Lets calculate the hash of the block
  DeviceEncryption::CalculateHash(data,
                                  {data.data() + offsetof(MetadataBlockHeader, hash), DeviceEncryption::DIGEST_SIZE});
  iv[0] ^= first_4_dwords[0].value();
  iv[1] ^= first_4_dwords[1].value();
  iv[2] ^= first_4_dwords[2].value();
  iv[3] ^= first_4_dwords[3].value();
  auto iv32 = iv[1];
  auto sectors_count = iv[2];
  auto sector_size = iv[3];
  if (std::popcount(sector_size) != 1) {
    // Not pow of 2
    return false;
  }
  auto log2_sector_size = std::bit_width(sector_size) - 1;

  device->SetLog2SectorSize(log2_sector_size);
  device->SetSectorsCount(sectors_count);
  block.reset();

  // Now try to fetch block again, this time check the hash.
  if (!Block::LoadMetadataBlock(blocks_device, block_number, block_size, iv32, /*load_data=*/true, /*check_hash=*/true)
           .has_value())
    return false;

  area_xor_wfs_iv =
      iv32 - ((block_number - area_start_block_number) << (log2_size(BlockSize::Physical) - log2_sector_size));
  return true;
}

}  // namespace

bool Recovery::CheckWfsKey(std::shared_ptr<Device> device, std::optional<std::vector<std::byte>> key) {
  auto blocks_device = std::make_shared<BlocksDevice>(device, key);
  auto block = *Block::LoadMetadataBlock(blocks_device, 0, BlockSize::Physical, /*iv=*/0, /*load_data=*/true,
                                         /*check_hash=*/false);
  auto wfs_header = reinterpret_cast<const WfsDeviceHeader*>(&block->data()[sizeof(MetadataBlockHeader)]);
  // Check wfs header
  return wfs_header->version.value() == WFS_VERSION;
}

std::optional<WfsError> Recovery::DetectDeviceParams(std::shared_ptr<Device> device,
                                                     std::optional<std::vector<std::byte>> key) {
  if (!CheckWfsKey(device, key))
    return WfsError::kInvalidWfsVersion;
  auto blocks_device = std::make_shared<BlocksDevice>(device, key);
  auto block = *Block::LoadMetadataBlock(blocks_device, /*physical_block_number=*/0, BlockSize::Physical, /*iv=*/0,
                                         /*load_data=*/true, /*check_hash=*/false);
  auto wfs_header = block->get_object<WfsDeviceHeader>(sizeof(MetadataBlockHeader));
  auto block_size = BlockSize::Physical;
  if (!(wfs_header->root_quota_metadata.flags.value() & EntryMetadata::Flags::AREA_SIZE_BASIC) &&
      (wfs_header->root_quota_metadata.flags.value() & EntryMetadata::Flags::AREA_SIZE_REGULAR))
    block_size = BlockSize::Logical;
  block.reset();

  if (!key.has_value()) {
    // This is a plain file, assume that the sector size is the default. It only matters right now for reencrypting when
    // we copy the parameters of the input device to the output. Once we won't do that, we could set the sectors count
    // according to the actual file size.
    // device->SetSectorsCount(device->GetFileSize() >> device->Log2SectorSize());
    device->SetSectorsCount(wfs_header->root_quota_metadata.quota_blocks_count.value()
                            << (static_cast<uint32_t>(block_size) - device->Log2SectorSize()));
    return std::nullopt;
  }

  uint32_t area_xor_wfs_iv;
  if (!RestoreMetadataBlockIVParameters(device, blocks_device, 0, 0, block_size, area_xor_wfs_iv)) {
    return WfsError::kAreaHeaderCorrupted;
  }
  return std::nullopt;
}

std::expected<std::shared_ptr<WfsDevice>, WfsError> Recovery::OpenWfsDeviceWithoutDeviceParams(
    std::shared_ptr<Device> device,
    std::optional<std::vector<std::byte>> key) {
  auto res = DetectDeviceParams(device, key);
  if (res.has_value())
    return std::unexpected(*res);
  return WfsDevice::Open(std::move(device), std::move(key));
}

namespace {
class FakeWfsDeviceHeaderBlocksDevice final : public BlocksDevice {
 public:
  FakeWfsDeviceHeaderBlocksDevice(std::shared_ptr<Device> device,
                                  std::optional<std::vector<std::byte>> key,
                                  std::vector<std::byte> wfs_header_block)
      : BlocksDevice(std::move(device), std::move(key)), wfs_header_block_(std::move(wfs_header_block)) {}
  ~FakeWfsDeviceHeaderBlocksDevice() final override = default;

  bool ReadBlock(uint32_t block_number,
                 uint32_t size_in_blocks,
                 const std::span<std::byte>& data,
                 const std::span<const std::byte>& hash,
                 uint32_t iv,
                 bool encrypt,
                 bool check_hash) override {
    if (block_number == 0) {
      if (data.size() != wfs_header_block_.size())
        return false;
      std::copy(wfs_header_block_.begin(), wfs_header_block_.end(), data.begin());
      return true;
    } else if (block_number < 0x1000) {
      // The first 0x1000 blocks not supperted and not supposed to be reached in this mode.
      return false;
    }
    return BlocksDevice::ReadBlock(block_number, size_in_blocks, data, hash, iv, encrypt, check_hash);
  }

 private:
  std::vector<std::byte> wfs_header_block_;
};

}  // namespace

std::expected<std::shared_ptr<WfsDevice>, WfsError> Recovery::OpenUsrDirectoryWithoutWfsDeviceHeader(
    std::shared_ptr<Device> device,
    std::optional<std::vector<std::byte>> key) {
  // First step: Read the /usr directory and restore root area ^ wfs ivs
  const uint32_t kUsrDirectoryBlockNumber = 0x1000;
  // Set the sectors count to be enough to read our block
  device->SetLog2SectorSize(9);
  device->SetSectorsCount((kUsrDirectoryBlockNumber + 2) << 3);
  auto blocks_device = std::make_shared<BlocksDevice>(device, key);
  auto block = *Block::LoadMetadataBlock(blocks_device, kUsrDirectoryBlockNumber, BlockSize::Physical, /*iv=*/0,
                                         /*load_data=*/true,
                                         /*check_hash=*/false);
  auto metadata_header = block->get_object<MetadataBlockHeader>(0);
  // Check wfs header
  if ((metadata_header->block_flags.value() >> 20) != 0xe00) {
    return std::unexpected(WfsError::kInvalidWfsVersion);
  }
  block.reset();

  uint32_t root_area_xor_wfs_iv;
  // Assume regular sized area
  if (!RestoreMetadataBlockIVParameters(device, blocks_device, kUsrDirectoryBlockNumber, 0, BlockSize::Logical,
                                        root_area_xor_wfs_iv)) {
    return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }

  // Create an initial fake first block, fill the important fields only for parsing
  MetadataBlockHeader header;
  std::memset(&header, 0, sizeof(header));
  header.block_flags = MetadataBlockHeader::Flags::AREA | MetadataBlockHeader::Flags::ROOT_AREA;
  WfsDeviceHeader wfs_header;
  std::memset(&wfs_header, 0, sizeof(wfs_header));
  wfs_header.iv = 0;
  wfs_header.version = 0x01010800;
  wfs_header.root_quota_metadata.flags = static_cast<uint32_t>(
      EntryMetadata::Flags::AREA_SIZE_REGULAR | EntryMetadata::Flags::QUOTA | EntryMetadata::Flags::DIRECTORY);
  WfsAreaHeader area_header;
  std::memset(&area_header, 0, sizeof(area_header));
  // This is the initial iv until we correct it
  area_header.iv = root_area_xor_wfs_iv;
  // Set the root directory to be /usr
  area_header.root_directory_block_number =
      kUsrDirectoryBlockNumber >> (log2_size(BlockSize::Logical) - log2_size(BlockSize::Physical));
  area_header.block_size_log2 = static_cast<uint8_t>(BlockSize::Logical);

  // Now create the block
  std::vector<std::byte> first_wfs_block;
  std::copy(reinterpret_cast<std::byte*>(&header), reinterpret_cast<std::byte*>(&header + 1),
            std::back_inserter(first_wfs_block));
  std::copy(reinterpret_cast<std::byte*>(&wfs_header), reinterpret_cast<std::byte*>(&wfs_header + 1),
            std::back_inserter(first_wfs_block));
  std::copy(reinterpret_cast<std::byte*>(&area_header), reinterpret_cast<std::byte*>(&area_header + 1),
            std::back_inserter(first_wfs_block));
  std::fill_n(std::back_inserter(first_wfs_block),
              (size_t{1} << log2_size(BlockSize::Logical)) - first_wfs_block.size(), std::byte{0});
  auto first_fixed_device = std::make_shared<FakeWfsDeviceHeaderBlocksDevice>(device, key, std::move(first_wfs_block));

  // Now go to the the /usr/system/save directory, there must be a sub-area there
  auto wfs_device = WfsDevice::Open(first_fixed_device);
  if (!wfs_device.has_value()) {
    throw std::logic_error("Failed to load wfs root block after fix");
  }
  auto system_save_dir = (*wfs_device)->GetDirectory("/save/system");
  if (!system_save_dir) {
    throw std::logic_error("Failed to find /usr/save/system");
  }
  std::shared_ptr<const Area> sub_area;
  for (const auto& [name, metadata] : system_save_dir->map_) {
    // this is probably a corrupted quota, let's check
    if (!metadata.get()->is_quota())
      continue;
    // ok this is quota
    auto new_quota =
        system_save_dir->quota()->LoadQuotaArea(metadata.get()->directory_block_number.value(), BlockSize::Logical);
    if (!new_quota.has_value())
      return std::unexpected(new_quota.error());
    sub_area = std::move(*new_quota);
    break;
  }
  if (!sub_area) {
    throw std::logic_error("Failed to find sub-quota under /usr/save/system");
  }
  uint32_t sub_area_xor_wfs_iv;
  if (!RestoreMetadataBlockIVParameters(
          device, blocks_device,
          sub_area->to_physical_block_number(sub_area->header()->root_directory_block_number.value()),
          sub_area->to_physical_block_number(0), BlockSize::Logical, sub_area_xor_wfs_iv)) {
    return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }
  uint32_t wfs_iv = sub_area_xor_wfs_iv ^ sub_area->header()->iv.value();
  uint32_t root_area_iv = root_area_xor_wfs_iv ^ wfs_iv;

  // Now fix the header
  wfs_header.iv = wfs_iv;
  area_header.iv = root_area_iv;

  // Recreate the first block
  first_wfs_block.clear();
  std::copy(reinterpret_cast<std::byte*>(&header), reinterpret_cast<std::byte*>(&header + 1),
            std::back_inserter(first_wfs_block));
  std::copy(reinterpret_cast<std::byte*>(&wfs_header), reinterpret_cast<std::byte*>(&wfs_header + 1),
            std::back_inserter(first_wfs_block));
  std::copy(reinterpret_cast<std::byte*>(&area_header), reinterpret_cast<std::byte*>(&area_header + 1),
            std::back_inserter(first_wfs_block));
  std::fill_n(std::back_inserter(first_wfs_block),
              (size_t{1} << log2_size(BlockSize::Logical)) - first_wfs_block.size(), std::byte{0});
  auto final_device = std::make_shared<FakeWfsDeviceHeaderBlocksDevice>(device, key, std::move(first_wfs_block));
  return WfsDevice::Open(std::move(final_device));
}
