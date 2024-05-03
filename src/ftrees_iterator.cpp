/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "ftrees_iterator.h"

FTreesIterator& FTreesIterator::operator++() {
  assert(!is_end());
  if (is_forward_) {
    ++ftrees_[index_].iterator;
  } else {
    // Switch direction
    key_type key = (**this).key;
    reverse_end_.reset(index_);
    // todo: enumrate
    for (auto& ftree : ftrees_) {
      while (!reverse_end_.test(ftree.node->index()) && !ftree.iterator.is_end() && (*++ftree.iterator).key < key) {
      }
    }
    is_forward_ = true;
    reverse_end_.reset();
  }
  index_ = find_next_extent_index(ftrees_, /*max=*/false, reverse_end_);
  return *this;
}

FTreesIterator& FTreesIterator::operator--() {
  assert(!is_begin());
  if (!is_forward_) {
    if (!reverse_end_.test(index_)) {
      --ftrees_[index_].iterator;
    }
  } else {
    // Switch direction
    key_type key = is_end() ? std::numeric_limits<key_type>::max() : (**this).key;
    for (auto& ftree : ftrees_) {
      if (ftree.iterator.is_begin()) {
        if (ftree.iterator.is_end() || (*ftree.iterator).key >= key) {
          reverse_end_.set(ftree.node->index());
        }
      } else {
        while (!ftree.iterator.is_begin() && (*--ftree.iterator).key > key) {
        }
      }
    }
    is_forward_ = false;
  }
  index_ = find_next_extent_index(ftrees_, /*max=*/true, reverse_end_);
  if (ftrees_[index_].iterator.is_begin()) {
    reverse_end_.set(index_);
  }
  return *this;
}

FTreesIterator FTreesIterator::operator++(int) {
  FTreesIterator tmp(*this);
  ++(*this);
  return tmp;
}

FTreesIterator FTreesIterator::operator--(int) {
  FTreesIterator tmp(*this);
  --(*this);
  return tmp;
}

bool FTreesIterator::is_begin() const {
  if (is_forward_) {
    return std::ranges::all_of(ftrees_, [](const ftree_info& ftree) { return ftree.iterator.is_begin(); });
  } else {
    return reverse_end_.all();
  }
}
