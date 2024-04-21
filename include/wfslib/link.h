/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>

#include "wfs_item.h"

class Area;

class Link : public WfsItem, public std::enable_shared_from_this<Link> {
 public:
  Link(std::string name, AttributesRef attributes, std::shared_ptr<Area> area)
      : WfsItem(std::move(name), std::move(attributes)), area_(std::move(area)) {}

 private:
  // TODO: We may have cyclic reference here if we do cache in area.
  std::shared_ptr<Area> area_;
};
