/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstdint>

namespace file_layout_constants {
inline constexpr uint8_t kMinMetadataLog2Size = 6;
inline constexpr uint8_t kMaxMetadataLog2Size = 10;
inline constexpr uint8_t kCategory0MaxMetadataLog2Size = 9;
inline constexpr uint32_t kCategory1MaxSingleBlocks = 5;
inline constexpr uint32_t kCategory2MaxLargeBlocks = 5;
inline constexpr uint32_t kCategory3MaxClusters = 4;
inline constexpr uint32_t kCategory4MaxMetadataBlocks = 237;
}  // namespace file_layout_constants
