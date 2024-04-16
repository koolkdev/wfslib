/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cassert>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "errors.h"

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

  struct HashRef {
    std::shared_ptr<Block> block;  // null if hash in same block
    size_t offset;
  };

  // TODO: Private constructor?
  Block(std::shared_ptr<BlocksDevice> device,
        uint32_t device_block_number,
        Block::BlockSize size_category,
        uint32_t data_size,
        uint32_t iv,
        HashRef hash_ref,
        bool encrypted);
  virtual ~Block();

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
    assert(offset + sizeof(T) <= size());
    return reinterpret_cast<const T*>(data().data() + offset);
  }

  template <typename T>
  T* get_mutable_object(size_t offset) {
    assert(offset + sizeof(T) <= size());
    return reinterpret_cast<T*>(mutable_data().data() + offset);
  }

  template <typename T>
  size_t to_offset(const T* obj) const {
    auto res = reinterpret_cast<const std::byte*>(obj) - data_.data();
    assert(res >= 0 && res < size());
    // TODO: [[assume(res >= 0)]]; and remove cast
    return static_cast<size_t>(res);
  }

  uint32_t device_block_number() const { return device_block_number_; }
  Block::BlockSize log2_size() const { return size_category_; }

  bool encrypted() const { return encrypted_; }

  void Resize(uint32_t data_size);

  void Detach();

  static std::expected<std::shared_ptr<Block>, WfsError> LoadDataBlock(std::shared_ptr<BlocksDevice> device,
                                                                       uint32_t device_block_number,
                                                                       Block::BlockSize size_category,
                                                                       uint32_t data_size,
                                                                       uint32_t iv,
                                                                       HashRef data_hash,
                                                                       bool encrypted,
                                                                       bool load_data = true,
                                                                       bool check_hash = true);

  static std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(std::shared_ptr<BlocksDevice> device,
                                                                           uint32_t device_block_number,
                                                                           Block::BlockSize size_category,
                                                                           uint32_t iv,
                                                                           bool load_data = true,
                                                                           bool check_hash = true);

 private:
  uint32_t GetAlignedSize(uint32_t size) const;

  std::span<std::byte> GetDataForWriting();

  std::byte* mutable_hash() {
    return (hash_ref_.block ? hash_ref_.block.get() : this)->get_mutable_object<std::byte>(hash_ref_.offset);
  }
  const std::byte* hash() const {
    return (hash_ref_.block ? hash_ref_.block.get() : this)->get_object<std::byte>(hash_ref_.offset);
  }

  std::shared_ptr<BlocksDevice> device_;

  uint32_t device_block_number_;
  Block::BlockSize size_category_;
  uint32_t data_size_;
  uint32_t iv_;
  bool encrypted_;

  bool dirty_{false};
  bool detached_{false};

  HashRef hash_ref_;
  // data buffer of at least size_, rounded to sector.
  std::vector<std::byte> data_;
};
