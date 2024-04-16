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
#include "file_device.h"
#include "structs.h"
#include "wfs.h"

namespace {

bool RestoreMetadataBlockIVParameters(std::shared_ptr<FileDevice> device,
                                      std::shared_ptr<BlocksDevice> blocks_device,
                                      uint32_t block_number,
                                      uint32_t area_start_block_number,
                                      Block::BlockSize block_size,
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
  device->SetSectorsCount((block_number << (Block::BlockSize::Basic - 9)) + (1 << (block_size - 9)));
  // This is the initial iv that the encryption will use
  std::array<uint32_t, 4> iv = {block_number << Block::BlockSize::Basic, 0, device->SectorsCount(),
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

  area_xor_wfs_iv = iv32 - ((block_number - area_start_block_number) << (Block::BlockSize::Basic - log2_sector_size));
  return true;
}

}  // namespace

bool Recovery::CheckWfsKey(std::shared_ptr<FileDevice> device, std::optional<std::vector<std::byte>> key) {
  auto blocks_device = std::make_shared<BlocksDevice>(device, key);
  auto block = *Block::LoadMetadataBlock(blocks_device, 0, Block::BlockSize::Basic, /*iv=*/0, /*load_data=*/true,
                                         /*check_hash=*/false);
  auto wfs_header = reinterpret_cast<const WfsHeader*>(&block->data()[sizeof(MetadataBlockHeader)]);
  // Check wfs header
  return wfs_header->version.value() == WFS_VERSION;
}

std::optional<WfsError> Recovery::DetectDeviceParams(std::shared_ptr<FileDevice> device,
                                                     std::optional<std::vector<std::byte>> key) {
  if (!CheckWfsKey(device, key))
    return WfsError::kInvalidWfsVersion;
  auto blocks_device = std::make_shared<BlocksDevice>(device, key);
  auto block = *Block::LoadMetadataBlock(blocks_device, /*device_block_number=*/0, Block::BlockSize::Basic, /*iv=*/0,
                                         /*load_data=*/true, /*check_hash=*/false);
  auto wfs_header = block->get_object<WfsHeader>(sizeof(MetadataBlockHeader));
  auto block_size = Block::BlockSize::Basic;
  if (!(wfs_header->root_quota_attributes.flags.value() & Attributes::Flags::AREA_SIZE_BASIC) &&
      (wfs_header->root_quota_attributes.flags.value() & Attributes::Flags::AREA_SIZE_REGULAR))
    block_size = Block::BlockSize::Regular;
  block.reset();
  uint32_t area_xor_wfs_iv;
  if (!RestoreMetadataBlockIVParameters(device, blocks_device, 0, 0, block_size, area_xor_wfs_iv)) {
    return WfsError::kAreaHeaderCorrupted;
  }
  return std::nullopt;
}

std::expected<std::shared_ptr<Wfs>, WfsError> Recovery::OpenWfsWithoutDeviceParams(
    std::shared_ptr<FileDevice> device,
    std::optional<std::vector<std::byte>> key) {
  auto res = DetectDeviceParams(device, key);
  if (res.has_value())
    return std::unexpected(*res);
  return Wfs::Load(std::make_shared<BlocksDevice>(std::move(device), key));
}

namespace {
class FakeWfsHeaderBlocksDevice final : public BlocksDevice {
 public:
  FakeWfsHeaderBlocksDevice(std::shared_ptr<Device> device,
                            std::optional<std::vector<std::byte>> key,
                            std::vector<std::byte> wfs_header_block)
      : BlocksDevice(std::move(device), std::move(key)), wfs_header_block_(std::move(wfs_header_block)) {}
  ~FakeWfsHeaderBlocksDevice() final override = default;

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

std::expected<std::shared_ptr<Wfs>, WfsError> Recovery::OpenUsrDirectoryWithoutWfsHeader(
    std::shared_ptr<FileDevice> device,
    std::optional<std::vector<std::byte>> key) {
  // First step: Read the /usr directory and restore root area ^ wfs ivs
  const uint32_t kUsrDirectoryBlockNumber = 0x1000;
  // Set the sectors count to be enough to read our block
  device->SetLog2SectorSize(9);
  device->SetSectorsCount((kUsrDirectoryBlockNumber + 2) << 3);
  auto blocks_device = std::make_shared<BlocksDevice>(device, key);
  auto block = *Block::LoadMetadataBlock(blocks_device, kUsrDirectoryBlockNumber, Block::BlockSize::Basic, /*iv=*/0,
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
  if (!RestoreMetadataBlockIVParameters(device, blocks_device, kUsrDirectoryBlockNumber, 0, Block::BlockSize::Regular,
                                        root_area_xor_wfs_iv)) {
    return std::unexpected(WfsError::kAreaHeaderCorrupted);
  }

  // Create an initial fake first block, fill the important fields only for parsing
  MetadataBlockHeader header;
  std::memset(&header, 0, sizeof(header));
  header.block_flags = MetadataBlockHeader::Flags::AREA | MetadataBlockHeader::Flags::ROOT_AREA;
  WfsHeader wfs_header;
  std::memset(&wfs_header, 0, sizeof(wfs_header));
  wfs_header.iv = 0;
  wfs_header.version = 0x01010800;
  wfs_header.root_quota_attributes.flags = static_cast<uint32_t>(
      Attributes::Flags::AREA_SIZE_REGULAR | Attributes::Flags::QUOTA | Attributes::Flags::DIRECTORY);
  WfsAreaHeader area_header;
  std::memset(&area_header, 0, sizeof(area_header));
  // This is the initial iv until we correct it
  area_header.iv = root_area_xor_wfs_iv;
  // Set the root directory to be /usr
  area_header.root_directory_block_number =
      kUsrDirectoryBlockNumber >> (Block::BlockSize::Regular - Block::BlockSize::Basic);
  area_header.block_size_log2 = Block::BlockSize::Regular;

  // Now create the block
  std::vector<std::byte> first_wfs_block;
  std::copy(reinterpret_cast<std::byte*>(&header), reinterpret_cast<std::byte*>(&header + 1),
            std::back_inserter(first_wfs_block));
  std::copy(reinterpret_cast<std::byte*>(&wfs_header), reinterpret_cast<std::byte*>(&wfs_header + 1),
            std::back_inserter(first_wfs_block));
  std::copy(reinterpret_cast<std::byte*>(&area_header), reinterpret_cast<std::byte*>(&area_header + 1),
            std::back_inserter(first_wfs_block));
  std::fill_n(std::back_inserter(first_wfs_block), (1 << Block::BlockSize::Regular) - first_wfs_block.size(),
              std::byte{0});
  auto first_fixed_device = std::make_shared<FakeWfsHeaderBlocksDevice>(device, key, std::move(first_wfs_block));

  // Now go to the the /usr/system/save directory, there must be a sub-area there
  auto wfs = Wfs::Load(first_fixed_device);
  if (!wfs.has_value()) {
    throw std::logic_error("Failed to load wfs root block after fix");
  }
  auto system_save_dir = (*wfs)->GetDirectory("/save/system");
  if (!system_save_dir) {
    throw std::logic_error("Failed to find /usr/save/system");
  }
  std::shared_ptr<const Area> sub_area;
  for (auto [name, item_or_error] : *system_save_dir) {
    if (item_or_error.has_value())
      continue;
    // this is probably a corrupted quota, let's check
    const auto attributes = system_save_dir->GetObjectAttributes(system_save_dir->block_, name);
    if (!attributes.has_value() || !attributes->data()->IsQuota())
      continue;
    // ok this is quota
    auto new_area =
        system_save_dir->area()->GetArea(attributes->data()->directory_block_number.value(), Block::BlockSize::Regular);
    if (!new_area.has_value())
      return std::unexpected(new_area.error());
    sub_area = std::move(*new_area);
    break;
  }
  if (!sub_area) {
    throw std::logic_error("Failed to find sub-quota under /usr/save/system");
  }
  uint32_t sub_area_xor_wfs_iv;
  if (!RestoreMetadataBlockIVParameters(
          device, blocks_device,
          sub_area->to_device_block_number(sub_area->header()->root_directory_block_number.value()),
          sub_area->to_device_block_number(0), Block::BlockSize::Regular, sub_area_xor_wfs_iv)) {
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
  std::fill_n(std::back_inserter(first_wfs_block), (1 << Block::BlockSize::Regular) - first_wfs_block.size(),
              std::byte{0});
  auto final_device = std::make_shared<FakeWfsHeaderBlocksDevice>(device, key, std::move(first_wfs_block));
  return Wfs::Load(std::move(final_device));
}
