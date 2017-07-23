/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <string>
#include <vector>

class KeyFile {
public:
	KeyFile(std::vector<uint8_t> data, size_t expected_size) : data(std::move(data)) {
		if (this->data.size() != expected_size)
			throw std::exception("Unexpected key file size");
	}

protected:
	template<class T>
	static T * LoadFromFile(std::string path, size_t size);

	std::vector<uint8_t> data;

	std::vector<uint8_t> GetKey(size_t offset, size_t size);
};

class OTP : KeyFile {
public:
	static const size_t OTP_SIZE = 0x400;

	OTP(std::vector<uint8_t> data) : KeyFile(data, OTP_SIZE) {}

	static OTP * LoadFromFile(std::string path);

	std::vector<uint8_t> GetMLCKey();
	std::vector<uint8_t> GetUSBSeedEncryptionKey();
};

class SEEPROM : KeyFile {
public:
	static const size_t SEEPROM_SIZE = 0x200;

	SEEPROM(std::vector<uint8_t> data) : KeyFile(data, SEEPROM_SIZE) {}

	static SEEPROM * LoadFromFile(std::string path);

	std::vector<uint8_t> GetUSBKeySeed();
	std::vector<uint8_t> GetUSBKey(OTP& otp);
};