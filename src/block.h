/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

class BlocksDevice;

class Block {
 public:
  enum BlockSize {
    Basic = 12,
    Regular = 13,
  };

  enum BlockSizeType {
    Single = 0,
    Large = 3,
    LargeCluster = 6,
  };

  bool Fetch(bool check_hash = true);
  void Flush();

  // Actual used size, always equal to capacity in metadata blocks.
  uint32_t size() const { return data_size_; }

  // Allocated size for the block
  uint32_t capacity() const { return 1 << log2_size(); }

  std::span<const std::byte> data() const { return {data_.data(), data_.data() + size()}; }
  // Accessing the non-const variant of data will mark the block as dirty.
  std::span<std::byte> mutable_data() { return GetDataForWriting(); }

  template <typename T>
  const T* get_object(size_t offset) const {
    return reinterpret_cast<const T*>(data().data() + offset);
  }

  template <typename T>
  T* get_mutable_object(size_t offset) {
    return reinterpret_cast<T*>(mutable_data().data() + offset);
  }

  // TODO: Rename to AbsBlockNumber for clarity?
  uint32_t BlockNumber() const { return block_number_; }
  Block::BlockSize log2_size() const { return size_category_; }

  bool encrypted() const { return encrypted_; }

  virtual void Resize(uint32_t data_size);

  void Detach();

  // TODO: fix private/protected
 private:
  uint32_t GetAlignedSize(uint32_t size) const;

  std::span<std::byte> GetDataForWriting();

 protected:
  Block(const std::shared_ptr<BlocksDevice>& device,
        uint32_t block_number,
        Block::BlockSize size_category,
        uint32_t data_size,
        uint32_t iv,
        bool encrypted);
  virtual ~Block();

  virtual std::span<std::byte> MutableHash() = 0;
  virtual std::span<const std::byte> Hash() const = 0;

  std::shared_ptr<BlocksDevice> device_;

  uint32_t block_number_;
  Block::BlockSize size_category_;
  uint32_t data_size_;
  uint32_t iv_;
  bool encrypted_;

  bool dirty_{false};
  bool detached_{false};

  // data buffer of at least size_, rounded to sector.
  std::vector<std::byte> data_;
};
