/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <catch2/catch_get_random_seed.hpp>
#include <random>
#include <ranges>

template <int N>
std::vector<uint32_t> createShuffledKeysArray() {
  auto array = std::ranges::to<std::vector<uint32_t>>(std::ranges::iota_view(0, N));
  std::ranges::shuffle(array, std::default_random_engine{Catch::getSeed()});
  return array;
}
