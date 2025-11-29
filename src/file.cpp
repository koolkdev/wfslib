/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "file.h"

uint32_t File::Size() const {
  return metadata()->file_size.value();
}

uint32_t File::SizeOnDisk() const {
  return metadata()->size_on_disk.value();
}

