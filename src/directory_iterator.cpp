/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_iterator.h"

#include "directory_map.h"
#include "entry.h"

DirectoryIterator::DirectoryIterator(DirectoryMapIterator base, std::shared_ptr<DirectoryMap> map)
    : base_(base), map_(std::move(map)) {}

DirectoryIterator::reference DirectoryIterator::operator*() const {
  auto val = *base_;
  auto metadata = map_->metadata_handle(val.name);
  if (!metadata.has_value())
    return {val.name, std::unexpected(metadata.error())};

  auto name = (*metadata)->metadata.get()->GetCaseSensitiveName(val.name);
  return {name, Entry::Load(base_.quota(), name, *metadata, map_, val.name)};
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
