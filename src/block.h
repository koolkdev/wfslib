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

  // Actual used size, always equal to capacity in metadata blocks.
  uint32_t size() const { return data_size_; }

  // Allocated size for the block
  uint32_t capacity() const { return 1 << log2_size(); }

  std::span<const std::byte> data() const { return {data_.data(), data_.data() + size()}; }
  // Accessing the non-const variant of data will mark the block as dirty.
  std::span<std::byte> data() { return GetDataForWriting(); }

  template <typename T>
  const T* get_object(size_t offset) const {
    return reinterpret_cast<const T*>(data().data() + offset);
  }

  template <typename T>
  T* get_object(size_t offset) {
    return reinterpret_cast<T*>(data().data() + offset);
  }

  uint32_t BlockNumber() const { return block_number_; }
  Block::BlockSize log2_size() const { return size_category_; }

  bool encrypted() const { return encrypted_; }

  class BadHash : public std::exception {
   public:
    BadHash(uint32_t block_number);
    virtual char const* what() const noexcept override;

   private:
    uint32_t block_number_;
    std::string msg_;
  };

  virtual void Resize(uint32_t data_size);

 private:
  uint32_t ToDeviceSector(uint32_t block_number) const;
  uint32_t GetAlignedSize(uint32_t size) const;

  std::span<std::byte> GetDataForWriting();

 protected:
  Block(const std::shared_ptr<DeviceEncryption>& device,
        uint32_t block_number,
        Block::BlockSize size_category,
        uint32_t data_size,
        uint32_t iv,
        bool encrypted);
  virtual ~Block();

  virtual std::span<std::byte> Hash() = 0;
  virtual std::span<const std::byte> Hash() const = 0;

  std::shared_ptr<DeviceEncryption> device_;

  uint32_t block_number_;
  Block::BlockSize size_category_;
  uint32_t data_size_;
  uint32_t iv_;
  bool encrypted_;

  bool dirty_{false};

  // data buffer of at least size_, rounded to sector.
  std::vector<std::byte> data_;
};
