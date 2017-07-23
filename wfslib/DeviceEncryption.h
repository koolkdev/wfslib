/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <vector>
#include <memory>
#include <cryptopp/sha.h>

class Device;
struct WfsBlockIV;

class DeviceEncryption {
public:
	DeviceEncryption(std::shared_ptr<Device> device, std::vector<uint8_t>& key);

	void WriteBlock(uint32_t sector_address, std::vector<uint8_t>& data, uint32_t iv);
	std::vector<uint8_t> ReadBlock(uint32_t sector_address, uint32_t length, uint32_t iv);

	void CalculateHash(std::vector<uint8_t>& data, std::vector<uint8_t>::iterator& hash, bool hash_in_block);
	bool CheckHash(std::vector<uint8_t>& data, std::vector<uint8_t>::iterator& hash, bool hash_in_block);

	std::shared_ptr<Device>& GetDevice() { return device; }

	static constexpr size_t DIGEST_SIZE = CryptoPP::SHA1::DIGESTSIZE;

private:
	void HashData(std::vector<uint8_t>& data, std::vector<uint8_t>::iterator& hash);
	WfsBlockIV GetIV(uint32_t sectors_count, uint32_t iv);
	size_t ToSectorSize(size_t size);

	std::shared_ptr<Device> device;

	std::vector<uint8_t> key;
};