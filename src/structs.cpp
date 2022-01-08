/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "structs.h"

inline size_t divide_round_up(size_t n, size_t div) {
  return (n + div - 1) / div;
}

size_t Attributes::DataOffset() {
  return offsetof(Attributes, case_bitmap) + divide_round_up(filename_length.value(), 8);
}

size_t InternalDirectoryTreeNode::size() {
  size_t total_size = sizeof(DirectoryTreeNode) + value_length.value() + choices_count.value() +
                      choices_count.value() * sizeof(boost::endian::big_uint16_buf_t);
  if (choices_count.value() > 0 && choices()[0] == std::byte{0})
    total_size += 2;
  return align_to_power_of_2(total_size);
}
