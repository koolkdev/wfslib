/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "errors.h"
#include "structs.h"

class EntryMetadataBuilder {
 public:
  struct NormalizedName {
    std::string key;
    std::vector<uint8_t> case_bitmap;
    uint8_t filename_length;
  };

  struct Attributes {
    Permissions permissions;
    uint32_t ctime;
    uint32_t mtime;
  };

  class Metadata {
   public:
    explicit Metadata(uint8_t metadata_log2_size);

    EntryMetadata* get();
    const EntryMetadata* get() const;
    size_t size() const;
    std::span<std::byte> mutable_bytes();

   private:
    std::vector<std::byte> bytes_;
  };

  static uint8_t Log2Size(size_t metadata_size);
  static uint8_t BaseLog2Size(uint8_t filename_length);

  static std::expected<NormalizedName, WfsError> NormalizeName(std::string_view name);

  static Metadata CreateEmptyFile(const NormalizedName& name, const Attributes& attributes, uint8_t block_size_log2);
  static Metadata CreateDirectory(const NormalizedName& name,
                                  const Attributes& attributes,
                                  uint32_t directory_block_number);
};
