/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <vector>
#include <stdexcept>
#include <string>

class DeviceEncryption;

class Block {
public:
	enum BlockSize {
		Basic = 12,
		Regular = 13,
		MegaBasic = 15,
		MegaRegular = 16,
	};

	virtual void Fetch(bool check_hash = true);
	virtual void Flush();

	std::vector<uint8_t>& GetData() { return data_; }
	uint32_t GetBlockNumber() { return block_number_;  }

	class BadHash : public std::exception {
	public:
		BadHash(uint32_t block_number);
		virtual char const* what() const noexcept override;
	private:
		uint32_t block_number;
		std::string msg;
	};
private:
	uint32_t ToDeviceSector(uint32_t block_number);

protected:
	Block(const std::shared_ptr<DeviceEncryption>& device, uint32_t block_number, Block::BlockSize size_category, uint32_t iv, bool encrypted, std::vector<uint8_t>&& data) :
		device_(device), block_number_(block_number), size_category_(size_category), iv_(iv), encrypted_(encrypted), data_(data) {
	}
	virtual ~Block() {}

	std::shared_ptr<DeviceEncryption> device_;

	uint32_t block_number_;
	Block::BlockSize size_category_;
	uint32_t iv_;
	bool encrypted_;

	// this vector will be rounded to sector after read
	std::vector<uint8_t> data_;
};
