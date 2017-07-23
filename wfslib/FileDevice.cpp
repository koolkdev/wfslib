/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "FileDevice.h"

#include <fstream>

FileDevice::FileDevice(const std::string& path, uint32_t log2_sector_size) : file(new std::fstream(path, std::ios::binary | std::ios::in)),
	log2_sector_size(log2_sector_size) {
	if (file->fail()) {
		throw std::exception("FileDevice: Failed to open file");
	}
	if (log2_sector_size < 9) {
		throw std::exception("FileDevice: Invalid sector size (<512)");
	}
	/*file->seekg(0, std::ios::end);
	if (file->tellg() >> 9 > UINT32_MAX) {
		throw std::exception("FileDevice: File too big! (>2TB)");
	}
	if (file->tellg() & ((1 << log2_sector_size) - 1)) {
		throw std::exception("FileDevice: Invalid file size. (Not multiply of sector size)");
	}
	sectors_count = static_cast<uint32_t>(file->tellg() >> log2_sector_size);*/
	sectors_count = 0x10; // we will find the exact sectors count later with Wfs::DetectSectorsCount
}

std::vector<uint8_t> FileDevice::ReadSectors(uint32_t sector_address, uint32_t sectors_count) {
	if (sector_address >= this->sectors_count || sector_address + sectors_count > this->sectors_count) {
		throw std::exception("FileDevice: Read out of file.");
	}
	std::lock_guard<std::mutex> guard(io_lock);
	file->seekg(static_cast<size_t>(sector_address) << log2_sector_size);
	std::vector<uint8_t> data(sectors_count << log2_sector_size);
	file->read(reinterpret_cast<char*>(&*data.begin()), data.size());
	if (file->gcount() != data.size())
		throw std::exception("FileDevice: Failed to read from file.");
	return data;

}
void FileDevice::WriteSectors(std::vector<uint8_t>& data, uint32_t sector_address, uint32_t sectors_count) {
	if (sector_address >= this->sectors_count || sector_address + sectors_count > this->sectors_count) {
		throw std::exception("FileDevice: Write out of file.");
	}
	if (data.size() < sectors_count << log2_sector_size) {
		throw std::exception("FileDevice: Not enough data for writing.");
	}
	std::lock_guard<std::mutex> guard(io_lock);
	file->seekp(static_cast<size_t>(sector_address) << log2_sector_size);
	file->write(reinterpret_cast<char*>(&*data.begin()), sectors_count << log2_sector_size);
	if (file->fail())
		throw std::exception("FileDevice: Failed to write to file.");
}