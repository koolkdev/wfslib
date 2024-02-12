/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs_item.h"

#include <boost/dynamic_bitset.hpp>
#include <cctype>
#include <ranges>
#include "metadata_block.h"
#include "structs.h"
#include "utils.h"

WfsItem::WfsItem(const std::string& name, const AttributesBlock& attributes) : name_(name), attributes_(attributes) {}

Attributes* AttributesBlock::Attributes() {
  return block->GetStruct<::Attributes>(attributes_offset);
}

const Attributes* AttributesBlock::Attributes() const {
  return as_const(block.get())->GetStruct<::Attributes>(attributes_offset);
}

bool WfsItem::IsDirectory() const {
  auto attributes = attributes_data().Attributes();
  return !attributes->IsLink() && attributes->IsDirectory();
}

bool WfsItem::IsFile() const {
  auto attributes = attributes_data().Attributes();
  return !attributes->IsLink() && !attributes->IsDirectory();
}

bool WfsItem::IsLink() const {
  auto attributes = attributes_data().Attributes();
  return attributes->IsLink();
}

std::string WfsItem::GetRealName() const {
  auto attributes = attributes_data().Attributes();
  std::string real_filename = "";
  if (attributes->filename_length.value() != name_.size()) {
    throw std::runtime_error("Unexepected filename length");
  }
  boost::dynamic_bitset<uint8_t> bits(name_.size());
  boost::from_block_range(reinterpret_cast<const uint8_t*>(&attributes->case_bitmap),
                          reinterpret_cast<const uint8_t*>(&attributes->case_bitmap) + (name_.size() / 8), bits);
  std::string real_name;
  std::ranges::transform(std::ranges::iota_view(0ull, name_.size()), std::back_inserter(real_name),
                         [&](auto i) { return bits[i] ? std::toupper(name_[i]) : name_[i]; });
  return real_name;
}
