/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <boost/dynamic_bitset.hpp>
#include <ranges>

#include "structs.h"

std::string EntryMetadata::GetCaseSensitiveName(std::string_view name) const {
  std::string real_filename = "";
  if (filename_length.value() != name.size()) {
    // TODO: return WfsError
    throw std::runtime_error("Unexepected filename length");
  }
  boost::dynamic_bitset<uint8_t> bits(name.size());
  boost::from_block_range(reinterpret_cast<const uint8_t*>(&case_bitmap),
                          reinterpret_cast<const uint8_t*>(&case_bitmap) + (name.size() / 8), bits);
  std::string final_name;
  std::ranges::transform(std::ranges::iota_view(0ull, name.size()), std::back_inserter(final_name),
                         [&](auto i) { return bits[i] ? std::toupper(name[i]) : std::tolower(name[i]); });
  return final_name;
}
