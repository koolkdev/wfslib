/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <string>

#include "block.h"
#include "structs.h"

class Area;

struct AttributesRef {
  std::shared_ptr<Block> block;
  size_t offset;

  const Attributes* get() const { return block->get_object<Attributes>(offset); }
  Attributes* get_mutable() const { return block->get_mutable_object<Attributes>(offset); }
};

class WfsItem {
 public:
  WfsItem(std::string name, AttributesRef block);
  virtual ~WfsItem();

  const std::string& name() const { return name_; }
  bool is_directory() const { return !attributes()->is_link() && attributes()->is_directory(); }
  bool is_file() const { return !attributes()->is_link() && !attributes()->is_directory(); }
  bool is_link() const { return attributes()->is_link(); }
  bool is_quota() const { return attributes()->is_directory() && attributes()->is_quota(); }

  static std::expected<std::shared_ptr<WfsItem>, WfsError> Load(std::shared_ptr<Area> area,
                                                                std::string name,
                                                                AttributesRef attributes_ref);

 protected:
  Attributes* mutable_attributes() { return attributes_.get_mutable(); }
  const Attributes* attributes() const { return attributes_.get(); }
  const std::shared_ptr<Block>& attributes_block() const { return attributes_.block; }

  std::string name_;
  AttributesRef attributes_;
};
