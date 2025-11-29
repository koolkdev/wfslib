/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "block.h"
#include "errors.h"
#include "quota_area.h"
#include "structs.h"
#include "utils.h"

namespace file_internal {

enum class DataType : uint8_t {
  InMetadata = 0,
  SingleBlocks = 1,
  LargeBlocks = 2,
  ClusterBlocks = 3,
  ExtendedClusterBlocks = 4,
};

inline size_t metadata_payload_capacity(const EntryMetadata& metadata) {
  auto total = size_t{1} << metadata.metadata_log2_size.value();
  auto base = metadata.size();
  if (total < base)
    throw WfsException(WfsError::kFileMetadataCorrupted);
  return total - base;
}

template <typename T>
inline std::span<const T> metadata_array(const EntryMetadata* metadata, size_t count, bool align_to_end) {
  if (count == 0)
    return {};
  auto capacity = metadata_payload_capacity(*metadata);
  auto required = count * sizeof(T);
  if (required > capacity)
    throw WfsException(WfsError::kFileMetadataCorrupted);
  auto base_bytes = reinterpret_cast<const std::byte*>(metadata);
  auto total = size_t{1} << metadata->metadata_log2_size.value();
  auto data_begin = align_to_end ? base_bytes + total - required : base_bytes + metadata->size();
  return {reinterpret_cast<const T*>(data_begin), count};
}

template <typename T>
inline std::span<T> mutable_metadata_array(EntryMetadata* metadata, size_t count, bool align_to_end) {
  if (count == 0)
    return {};
  auto capacity = metadata_payload_capacity(*metadata);
  auto required = count * sizeof(T);
  if (required > capacity)
    throw WfsException(WfsError::kFileMetadataCorrupted);
  auto base_bytes = reinterpret_cast<std::byte*>(metadata);
  auto total = size_t{1} << metadata->metadata_log2_size.value();
  auto data_begin = align_to_end ? base_bytes + total - required : base_bytes + metadata->size();
  return {reinterpret_cast<T*>(data_begin), count};
}

template <typename Span>
inline Span make_subspan(Span span, size_t offset, size_t length) {
  if (offset >= span.size() || length == 0)
    return span.subspan(span.size(), 0);
  length = std::min(length, span.size() - offset);
  return span.subspan(offset, length);
}

inline size_t block_capacity_log2(size_t base_log2, BlockType type) {
  return base_log2 + log2_size(type);
}

inline size_t clusters_per_metadata_block(const QuotaArea& quota) {
  auto block_payload = quota.block_size() - sizeof(MetadataBlockHeader);
  auto capacity = block_payload / sizeof(DataBlocksClusterMetadata);
  return std::min<size_t>(capacity, 48);
}

}  // namespace file_internal

