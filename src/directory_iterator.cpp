/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_iterator.h"

#include "directory_map.h"
#include "entry.h"

DirectoryIterator::DirectoryIterator(std::shared_ptr<DirectoryMap> map, DirectoryMapIterator base)
    : map_(std::move(map)), base_(std::move(base)) {}

DirectoryIterator::reference DirectoryIterator::operator*() const {
  auto val = *base_;
  auto name = val.metadata.get()->GetCaseSensitiveName(val.name);
  auto entry = map_->LoadEntry(base_);
  if (!entry.has_value())
    return {name, std::unexpected(entry.error())};
  return {std::move(name), std::move(*entry)};
}

DirectoryIterator& DirectoryIterator::operator++() {
  ++base_;
  return *this;
}

DirectoryIterator& DirectoryIterator::operator--() {
  --base_;
  return *this;
}

DirectoryIterator DirectoryIterator::operator++(int) {
  DirectoryIterator tmp(*this);
  ++(*this);
  return tmp;
}

DirectoryIterator DirectoryIterator::operator--(int) {
  DirectoryIterator tmp(*this);
  --(*this);
  return tmp;
}
