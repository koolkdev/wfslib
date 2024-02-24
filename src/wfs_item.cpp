/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs_item.h"

#include "metadata_block.h"
#include "structs.h"
#include "utils.h"

WfsItem::WfsItem(const std::string& name, const AttributesBlock& attributes) : name_(name), attributes_(attributes) {}

Attributes* AttributesBlock::mutable_data() {
  return block->get_mutable_object<::Attributes>(attributes_offset);
}

const Attributes* AttributesBlock::data() const {
  return block->get_object<::Attributes>(attributes_offset);
}

bool WfsItem::IsDirectory() const {
  auto attributes = attributes_data().data();
  return !attributes->IsLink() && attributes->IsDirectory();
}

bool WfsItem::IsFile() const {
  auto attributes = attributes_data().data();
  return !attributes->IsLink() && !attributes->IsDirectory();
}

bool WfsItem::IsLink() const {
  return attributes_data().data()->IsLink();
}

bool WfsItem::IsQuota() const {
  auto attributes = attributes_data().data();
  return attributes->IsDirectory() && attributes->IsQuota();
}
