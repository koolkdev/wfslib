/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include "directory_map_iterator.h"
#include "errors.h"

class QuotaArea;
class Entry;

struct DiretoryEntry {
  std::string name;
  std::expected<std::shared_ptr<Entry>, WfsError> entry;
};

class DirectoryIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = ptrdiff_t;

  using value_type = DiretoryEntry;
  using ref_type = DiretoryEntry;

  using reference = ref_type;

  DirectoryIterator() = default;
  DirectoryIterator(DirectoryMapIterator base);

  reference operator*() const;

  DirectoryIterator& operator++();
  DirectoryIterator& operator--();
  DirectoryIterator operator++(int);
  DirectoryIterator operator--(int);

  bool operator==(const DirectoryIterator& other) const { return base_ == other.base_; }

  DirectoryMapIterator& base() { return base_; };
  const DirectoryMapIterator& base() const { return base_; };

  bool is_begin() const { return base_.is_begin(); }
  bool is_end() const { return base_.is_end(); }

 private:
  DirectoryMapIterator base_;
};
