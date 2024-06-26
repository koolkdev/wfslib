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

class QuotaArea;

using AttributesRef = Block::DataRef<Attributes>;

class WfsItem {
 public:
  WfsItem(std::string name, AttributesRef block);
  virtual ~WfsItem();

  const std::string& name() const { return name_; }
  bool is_directory() const { return !attributes()->is_link() && attributes()->is_directory(); }
  bool is_file() const { return !attributes()->is_link() && !attributes()->is_directory(); }
  bool is_link() const { return attributes()->is_link(); }
  bool is_quota() const { return attributes()->is_directory() && attributes()->is_quota(); }

  static std::expected<std::shared_ptr<WfsItem>, WfsError> Load(std::shared_ptr<QuotaArea> quota,
                                                                std::string name,
                                                                AttributesRef attributes_ref);

 protected:
  // TODO: Attributes copy as it can change?
  Attributes* mutable_attributes() { return attributes_.get_mutable(); }
  const Attributes* attributes() const { return attributes_.get(); }
  const std::shared_ptr<Block>& attributes_block() const { return attributes_.block; }

  std::string name_;
  AttributesRef attributes_;
};
