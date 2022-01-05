/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs_item.h"

#include <cctype>
#include "metadata_block.h"
#include "structs.h"

WfsItem::WfsItem(const std::string& name, const AttributesBlock& attributes) : name_(name), attributes_(attributes) {}

Attributes* AttributesBlock::Attributes() const {
  if (!block)
    return NULL;
  return reinterpret_cast<::Attributes*>(&block->GetData()[attributes_offset]);
}

bool WfsItem::IsDirectory() {
  auto attributes = attributes_.Attributes();
  return !attributes->IsLink() && attributes->IsDirectory();
}

bool WfsItem::IsFile() {
  auto attributes = attributes_.Attributes();
  return !attributes->IsLink() && !attributes->IsDirectory();
}

bool WfsItem::IsLink() {
  auto attributes = attributes_.Attributes();
  return attributes->IsLink();
}

std::string WfsItem::GetRealName() {
  auto attributes = attributes_.Attributes();
  auto& filename = name_;
  std::string real_filename = "";
  if (attributes->filename_length.value() != filename.size()) {
    throw std::runtime_error("Unexepected filename length");
  }
  uint8_t* bitmap_pos = reinterpret_cast<uint8_t*>(&attributes->case_bitmap);
  uint8_t cur = 0, i = 0;
  for (char c : name_) {
    if (i++ % 8 == 0) {
      cur = *bitmap_pos++;
    }
    if (cur & 1)
      c = static_cast<char>(std::toupper(c));
    cur >>= 1;
    real_filename += c;
  }
  return real_filename;
}