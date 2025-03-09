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
#include <vector>

#include "errors.h"

class BlocksDevice;

class Block;

template <typename T>
concept BlockRefObj = requires(const T& block_ref) {
  { block_ref.operator->() } -> std::same_as<Block*>;
};

template <typename T>
concept BlockRef = std::same_as<T, Block*> || BlockRefObj<T>;

enum class BlockSize : int {
  Physical = 12,
  Logical = 13,
};

enum class BlockType : int {
  Single = 0,
  Large = 3,
  Cluster = 6,
};

constexpr inline auto log2_size(BlockSize size) {
  return static_cast<int>(size);
}

constexpr inline auto log2_size(BlockType size) {
  return static_cast<int>(size);
}

class Block {
 public:
  template <typename T, BlockRef BlockRefType>
  struct DataRefBase {
    BlockRefType block;
    size_t offset;

    const T* get() const { return block->template get_object<T>(offset); }
    T* get_mutable() const { return block->template get_mutable_object<T>(offset); }

    auto operator<=>(const DataRefBase& other) const {
      if (const auto res = block <=> other.block; res != 0)
        return res;
      return offset <=> other.offset;
    }
    bool operator==(const DataRefBase& other) const { return block == other.block && offset == other.offset; }

    const T* operator->() const { return get(); }
    T* operator->() { return get_mutable(); }
  };

  template <typename T>
  struct DataRef : public DataRefBase<T, std::shared_ptr<Block>> {};
  template <typename T>
  struct RawDataRef : public DataRefBase<T, Block*> {};

  struct HashRef {
    std::shared_ptr<Block> block;  // null if hash in same block
    size_t offset;
  };

  // TODO: Private constructor?
  Block(std::shared_ptr<BlocksDevice> device,
        uint32_t physical_block_number,
        BlockSize block_size,
        BlockType block_type,
        uint32_t data_size,
        uint32_t iv,
        HashRef hash_ref,
        bool encrypted);
  Block(std::vector<std::byte> data);
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
    assert(res >= 0 && res < static_cast<std::ptrdiff_t>(size()));
    // TODO: [[assume(res >= 0)]]; and remove cast
    return static_cast<size_t>(res);
  }

  uint32_t physical_block_number() const { return physical_block_number_; }
  BlockSize block_size() const { return block_size_; }
  BlockType block_type() const { return block_type_; }
  int log2_size() const { return ::log2_size(block_size_) + ::log2_size(block_type_); }

  bool encrypted() const { return encrypted_; }

  void Resize(uint32_t data_size);

  void Detach();

  static std::expected<std::shared_ptr<Block>, WfsError> LoadDataBlock(std::shared_ptr<BlocksDevice> device,
                                                                       uint32_t physical_block_number,
                                                                       BlockSize block_size,
                                                                       BlockType block_type,
                                                                       uint32_t data_size,
                                                                       uint32_t iv,
                                                                       HashRef data_hash,
                                                                       bool encrypted,
                                                                       bool load_data = true,
                                                                       bool check_hash = true);

  static std::expected<std::shared_ptr<Block>, WfsError> LoadMetadataBlock(std::shared_ptr<BlocksDevice> device,
                                                                           uint32_t physical_block_number,
                                                                           BlockSize block_size,
                                                                           uint32_t iv,
                                                                           bool load_data = true,
                                                                           bool check_hash = true);

  static std::shared_ptr<Block> CreateDetached(std::vector<std::byte> data);

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

  uint32_t physical_block_number_;
  BlockSize block_size_;
  BlockType block_type_;
  uint32_t data_size_;
  uint32_t iv_;
  bool encrypted_;

  bool dirty_{false};
  bool detached_{false};

  HashRef hash_ref_;
  // data buffer of at least size_, rounded to sector.
  std::vector<std::byte> data_;
};
