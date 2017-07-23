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


Wfs::Wfs(std::shared_ptr<Device> device, std::vector<uint8_t>& key) : device(std::make_shared<DeviceEncryption>(device, key)) {
	// Read first area
	this->root_area = Area::LoadRootArea(this);
}

std::shared_ptr<File> Wfs::GetFile(const std::string filename) {
	boost::filesystem::path path(filename);
	auto dir = GetDirectory(path.parent_path().string());
	if (!dir) return std::shared_ptr<File>();
	return dir->GetFile(path.filename().string());
}

std::shared_ptr<Directory> Wfs::GetDirectory(const std::string filename) {
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

void Wfs::DetectSectorsCount(std::shared_ptr<FileDevice> device, std::vector<uint8_t>& key) {
	auto block = MetadataBlock::LoadBlock(std::make_shared<DeviceEncryption>(device, key), 0, Block::BlockSize::Basic, 0, false);
	auto wfs_header = reinterpret_cast<WfsHeader *>(&block->GetData()[sizeof(MetadataBlockHeader)]);
	if (wfs_header->version.value() != 0x01010800)
		throw std::exception("Unexpected WFS version (bad key?)");
	auto block_size = Block::BlockSize::Basic;
	if (!(wfs_header->root_area_attributes.flags.value() & wfs_header->root_area_attributes.Flags::AREA_SIZE_BASIC) && (wfs_header->root_area_attributes.flags.value() & wfs_header->root_area_attributes.Flags::AREA_SIZE_REGULAR))
		block_size = Block::BlockSize::Regular;
	device->SetSectorsCount(wfs_header->root_area_attributes.blocks_count.value() << (block_size - device->GetLog2SectorSize()));
}