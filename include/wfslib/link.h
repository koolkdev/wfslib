/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>

#include "entry.h"

class QuotaArea;

class Link : public Entry, public std::enable_shared_from_this<Link> {
 public:
  Link(std::string name, AttributesRef attributes, std::shared_ptr<QuotaArea> quota)
      : Entry(std::move(name), std::move(attributes)), quota_(std::move(quota)) {}

 private:
  // TODO: We may have cyclic reference here if we do cache in area.
  std::shared_ptr<QuotaArea> quota_;
};
