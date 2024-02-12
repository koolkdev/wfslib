/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

class DeviceEncryption;

class Block {
 public:
  enum BlockSize {
    Basic = 12,
    Regular = 13,
    MegaBasic = 15,
    MegaRegular = 16,
  };

  void Fetch(bool check_hash = true);
  void Flush();

  // The actual size of the data, may be smaller than the allocated size.
  size_t data_size() const { return data_.size(); }

  std::span<const std::byte> Data() const { return GetData(true); }
  // Accessing the non-const variant of data will mark the block as dirty.
  std::span<std::byte> Data() { return GetData(); }

  template <typename T>
  const T* GetStruct(size_t offset) const {
    return reinterpret_cast<const T*>(Data().data() + offset);
  }

  template <typename T>
  T* GetStruct(size_t offset) {
    return reinterpret_cast<T*>(Data().data() + offset);
  }

  void Resize(size_t new_size);
  uint32_t BlockNumber() const { return block_number_; }
  Block::BlockSize log2_size() const { return size_category_; }

  class BadHash : public std::exception {
   public:
    BadHash(uint32_t block_number);
    virtual char const* what() const noexcept override;

   private:
    uint32_t block_number_;
    std::string msg_;
  };

 private:
  uint32_t ToDeviceSector(uint32_t block_number);

  std::span<std::byte> GetData(bool read_only = false);

 protected:
  Block(const std::shared_ptr<DeviceEncryption>& device,
        uint32_t block_number,
        Block::BlockSize size_category,
        uint32_t iv,
        bool encrypted,
        std::vector<std::byte>&& data);
  virtual ~Block() = default;

  virtual std::span<std::byte> Hash() = 0;
  virtual std::span<const std::byte> Hash() const = 0;

  std::shared_ptr<DeviceEncryption> device_;

  uint32_t block_number_;
  Block::BlockSize size_category_;
  uint32_t iv_;
  bool encrypted_;

  bool dirty_{false};

  // this vector will be rounded to sector after read
  std::vector<std::byte> data_;
};
