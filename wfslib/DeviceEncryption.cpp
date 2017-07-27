/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "DeviceEncryption.h"
#include <boost/endian/buffers.hpp> 
#include <botan/sha160.h>

#include "Device.h"

struct WfsBlockIV {
	boost::endian::big_uint32_buf_t iv[4];
};

DeviceEncryption::DeviceEncryption(const std::shared_ptr<Device>& device, const std::vector<uint8_t>& key) : device(device), 
		encryptor(Botan::get_cipher_mode("AES-128/CBC", Botan::ENCRYPTION)),
		decryptor(Botan::get_cipher_mode("AES-128/CBC", Botan::DECRYPTION)) {
	encryptor->set_key(key);
	decryptor->set_key(key);

}


void DeviceEncryption::HashData(const std::vector<uint8_t>& data, const std::vector<uint8_t>::iterator& hash) {
	// Pad and hash
	Botan::SHA_160 sha;
	sha.update(data);
	std::vector<uint8_t> pad(ToSectorSize(data.size()) - data.size(), 0);
	if (!pad.empty())
		sha.update(pad);
	sha.final(&*hash);
}

void DeviceEncryption::CalculateHash(const std::vector<uint8_t>& data, const std::vector<uint8_t>::iterator& hash, bool hash_in_block) {
	// Fill hash space with 0xFF
	if (hash_in_block)
		std::fill(hash, hash + DIGEST_SIZE, 0xFF);

	HashData(data, hash);
}

void DeviceEncryption::WriteBlock(uint32_t sector_address, std::vector<uint8_t>& data, uint32_t iv) {
	// Pad with zeros
	data = std::vector<uint8_t>(data);
	data.resize(ToSectorSize(data.size()), 0);

	uint32_t sectors_count = static_cast<uint32_t>(data.size() / this->device->GetSectorSize());

	// Encrypt
	encryptor->start(GetIV(sectors_count, iv));
	encryptor->process(&data[0], data.size());

	// Write
	this->device->WriteSectors(data, sector_address, sectors_count);
}

std::vector<uint8_t> DeviceEncryption::ReadBlock(uint32_t sector_address, uint32_t length, uint32_t iv) {
	uint8_t sectors_count = static_cast<uint8_t>(ToSectorSize(length) / this->device->GetSectorSize());

	std::vector<uint8_t> data = this->device->ReadSectors(sector_address, sectors_count);

	decryptor->start(GetIV(sectors_count, iv));
	decryptor->process(&data[0], data.size());

	//data.resize(length);
	return data;
}

bool DeviceEncryption::CheckHash(const std::vector<uint8_t>& data, const std::vector<uint8_t>::iterator& hash, bool hash_in_block) {
	std::vector<uint8_t> placeholder_hash(DIGEST_SIZE, 0xFF);
	if (hash_in_block)
		std::swap_ranges(placeholder_hash.begin(), placeholder_hash.end(), hash);

	std::vector<uint8_t> calculated_hash(DIGEST_SIZE);
	HashData(data, calculated_hash.begin());

	 if (hash_in_block)
		 std::swap_ranges(placeholder_hash.begin(), placeholder_hash.end(), hash);

	 return std::equal(calculated_hash.begin(), calculated_hash.end(), hash);
}

std::vector<uint8_t> DeviceEncryption::GetIV(uint32_t sectors_count, uint32_t iv) {
	std::vector<uint8_t> _iv(sizeof(WfsBlockIV));
	WfsBlockIV* aes_iv = reinterpret_cast<WfsBlockIV*>(&_iv[0]);
	aes_iv->iv[0] = sectors_count * this->device->GetSectorSize();
	aes_iv->iv[1] = iv;
	aes_iv->iv[2] = this->device->GetSectorsCount();
	aes_iv->iv[3] = this->device->GetSectorSize();
	return _iv;
}

size_t DeviceEncryption::ToSectorSize(size_t size) {
	return size + (-static_cast<int32_t>(size)) % this->device->GetSectorSize();
}