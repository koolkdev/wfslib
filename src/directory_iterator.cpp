/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_iterator.h"

#include "entry.h"
#include "quota_area.h"

DirectoryIterator::DirectoryIterator(DirectoryMapIterator base) : base_(base) {}

DirectoryIterator::reference DirectoryIterator::operator*() const {
  auto val = *base_;
  auto name = val.attributes.get()->GetCaseSensitiveName(val.name);
  return {name, Entry::Load(base_.quota(), name, val.attributes)};
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
