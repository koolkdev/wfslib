/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "errors.h"

char const* WfsException::what() const noexcept {
  switch (error_) {
    case WfsError::kItemNotFound:
      return "Item not found";
    case WfsError::kNotDirectory:
      return "Not a directory";
    case WfsError::kNotFile:
      return "Not a file";
    case WfsError::kBlockBadHash:
      return "Block bad hash";
    case WfsError::kAreaHeaderCorrupted:
      return "Area header corrupted";
    case WfsError::kDirectoryCorrupted:
      return "Directory corrupted";
  }
  return "";
}
