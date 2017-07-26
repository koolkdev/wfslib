/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "Wfs.h"
#include "DeviceEncryption.h"
#include "Area.h"
#include "Directory.h"
#include "FileDevice.h"
#include "MetadataBlock.h"
#include "Structs.h"

#include <boost/filesystem.hpp>


Wfs::Wfs(const std::shared_ptr<Device>& device, std::vector<uint8_t>& key) : device(std::make_shared<DeviceEncryption>(device, key)) {
	// Read first area
	this->root_area = Area::LoadRootArea(this->device);
}

std::shared_ptr<WfsItem> Wfs::GetObject(const std::string& filename) {
	if (filename == "/") return GetDirectory("/");
	boost::filesystem::path path(filename);
	auto dir = GetDirectory(path.parent_path().string());
	if (!dir) return std::shared_ptr<WfsItem>();
	return dir->GetObject(path.filename().string());
}

std::shared_ptr<File> Wfs::GetFile(const std::string& filename) {
	boost::filesystem::path path(filename);
	auto dir = GetDirectory(path.parent_path().string());
	if (!dir) return std::shared_ptr<File>();
	return dir->GetFile(path.filename().string());
}

std::shared_ptr<Directory> Wfs::GetDirectory(const std::string& filename) {
	boost::filesystem::path path(filename);
	std::shared_ptr<Directory> current_directory = this->root_area->GetRootDirectory();

	for (auto& part : path) {
		// the first part is "/"
		if (part == "/") continue;
		current_directory = current_directory->GetDirectory(part.string());
		if (!current_directory) return current_directory;
	}
	return current_directory;
}

void Wfs::DetectSectorsCount(const std::shared_ptr<FileDevice>& device, const std::vector<uint8_t>& key) {
	auto block = MetadataBlock::LoadBlock(std::make_shared<DeviceEncryption>(device, key), 0, Block::BlockSize::Basic, 0, false);
	auto wfs_header = reinterpret_cast<WfsHeader *>(&block->GetData()[sizeof(MetadataBlockHeader)]);
	if (wfs_header->version.value() != 0x01010800)
		throw std::runtime_error("Unexpected WFS version (bad key?)");
	auto block_size = Block::BlockSize::Basic;
	if (!(wfs_header->root_area_attributes.flags.value() & wfs_header->root_area_attributes.Flags::AREA_SIZE_BASIC) && (wfs_header->root_area_attributes.flags.value() & wfs_header->root_area_attributes.Flags::AREA_SIZE_REGULAR))
		block_size = Block::BlockSize::Regular;
	device->SetSectorsCount(wfs_header->root_area_attributes.blocks_count.value() << (block_size - device->GetLog2SectorSize()));
}

void Wfs::DetectSectorSize(const std::shared_ptr<FileDevice>& device, const std::vector<uint8_t>& key) {
	bool detected = false;
	for (auto sector_size : {9, 11, 12}) {
		device->SetLog2SectorSize(sector_size);
		if (VerifyDeviceAndKey(device, key)) {
			detected = true;
			break;
		}
	}
	if (!detected) {
		throw std::runtime_error("Wfs: Failed to detect sector size");
	}
}

bool Wfs::VerifyDeviceAndKey(const std::shared_ptr<FileDevice>& device, const std::vector<uint8_t>& key) {
	auto enc_device = std::make_shared<DeviceEncryption>(device, key);
	try {
		MetadataBlock::LoadBlock(enc_device, 0, Block::BlockSize::Basic, 0, true);
	}
	catch (Block::BadHash) {
		try {
			MetadataBlock::LoadBlock(enc_device, 0, Block::BlockSize::Regular, 0, true);
		}
		catch (Block::BadHash) {
			return false;
		}
	}
	return true;
}