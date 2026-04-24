/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "structs.h"

template <std::ranges::input_range Range, typename Projection>
auto CollectRange(Range&& range, Projection projection) {
  using Value = std::remove_cvref_t<std::invoke_result_t<Projection, std::ranges::range_reference_t<Range>>>;
  std::vector<Value> values;
  for (auto&& item : range) {
    values.push_back(std::invoke(projection, item));
  }
  return values;
}

template <std::ranges::input_range Range>
auto CollectKeys(Range&& range) {
  return CollectRange(std::forward<Range>(range), [](const auto& item) -> uint32_t { return item.key(); });
}

template <std::ranges::input_range Range>
auto CollectKeyValues(Range&& range) {
  return CollectRange(std::forward<Range>(range), [](const auto& item) -> std::pair<uint32_t, uint32_t> {
    return {item.key(), item.value()};
  });
}

template <std::ranges::input_range Range>
auto CollectFreeExtents(Range&& range) {
  return CollectRange(std::forward<Range>(range), [](const auto& item) -> std::tuple<uint32_t, nibble, size_t> {
    return {item.key(), item.value(), item.bucket_index};
  });
}

inline std::vector<uint32_t> SequentialKeys(uint32_t count, uint32_t start = 0, uint32_t step = 1) {
  std::vector<uint32_t> keys;
  keys.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    keys.push_back(start + i * step);
  }
  return keys;
}

inline std::vector<std::pair<uint32_t, uint32_t>> SequentialKeyValues(uint32_t count) {
  std::vector<std::pair<uint32_t, uint32_t>> values;
  values.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    values.emplace_back(i, i + 1);
  }
  return values;
}

inline std::vector<std::pair<uint32_t, nibble>> SequentialNibbleValues(uint32_t count) {
  std::vector<std::pair<uint32_t, nibble>> values;
  values.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    values.emplace_back(i, static_cast<nibble>(i % 16));
  }
  return values;
}

inline std::vector<std::tuple<uint32_t, nibble, size_t>> SequentialFreeExtents(uint32_t count, size_t bucket_index) {
  std::vector<std::tuple<uint32_t, nibble, size_t>> values;
  values.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    values.emplace_back(i, static_cast<nibble>(i % 16), bucket_index);
  }
  return values;
}

template <typename Tree, typename KeyProjection>
void RequireBidirectionalIteration(Tree& tree,
                                   uint32_t expected_count,
                                   KeyProjection key_projection,
                                   uint32_t forward_steps = 40,
                                   uint32_t backward_steps = 20) {
  auto it = tree.begin();
  uint32_t steps = 0;
  while (it != tree.end()) {
    REQUIRE(std::invoke(key_projection, *it) == steps);
    ++it;
    ++steps;
  }
  REQUIRE(steps == expected_count);
  REQUIRE(it.is_end());

  while (it != tree.begin()) {
    --it;
    --steps;
    REQUIRE(std::invoke(key_projection, *it) == steps);
  }
  REQUIRE(steps == 0);
  REQUIRE(it.is_begin());

  for (uint32_t i = 0; i < forward_steps; ++i) {
    ++it;
    ++steps;
    REQUIRE(std::invoke(key_projection, *it) == steps);
  }
  for (uint32_t i = 0; i < backward_steps; ++i) {
    --it;
    --steps;
    REQUIRE(std::invoke(key_projection, *it) == steps);
  }
  REQUIRE(std::invoke(key_projection, *it) == forward_steps - backward_steps);
}
