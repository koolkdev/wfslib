/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <boost/dynamic_bitset.hpp>
#include <ranges>

#include "structs.h"
#include "utils.h"

std::string Attributes::GetCaseSensitiveName(const std::string& name) const {
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

size_t ExternalDirectoryTreeNode::size() const {
  size_t total_size = sizeof(DirectoryTreeNode) + prefix_length.value() + choices_count.value() +
                      choices_count.value() * sizeof(uint16_be_t);
  return align_to_power_of_2(total_size);
}

size_t InternalDirectoryTreeNode::size() const {
  size_t total_size = sizeof(DirectoryTreeNode) + prefix_length.value() + choices_count.value() +
                      choices_count.value() * sizeof(uint16_be_t);
  if (choices_count.value() > 0 && choices()[0] == std::byte{0})
    total_size += 2;
  return align_to_power_of_2(total_size);
}
