/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <vector>
#include <memory>

class Device;
class FileDevice;
class DeviceEncryption;
class Area;
class File;
class Directory;

class Wfs {
public:
	Wfs(std::shared_ptr<Device> device, std::vector<uint8_t>& key);

	std::shared_ptr<DeviceEncryption> GetDevice() {
		return device;
	}

	std::shared_ptr<File> GetFile(const std::string filename);
	std::shared_ptr<Directory> GetDirectory(const std::string filename);

	static void DetectSectorsCount(std::shared_ptr<FileDevice> device, std::vector<uint8_t>& key);

private:
	std::shared_ptr<DeviceEncryption> device;

	friend class Area;
	std::shared_ptr<Area> root_area;
};