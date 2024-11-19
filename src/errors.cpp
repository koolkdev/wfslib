/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "errors.h"

char const* WfsException::what() const noexcept {
  switch (error_) {
    case WfsError::kEntryNotFound:
      return "Entry not found";
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
    case WfsError::kFreeBlocksAllocatorCorrupted:
      return "Free blocks allocator corrupted";
    case WfsError::kFileDataCorrupted:
      return "File data corrupted";
    case WfsError::kFileMetadataCorrupted:
      return "File metadata corrupted";
    case WfsError::kTransactionsAreaCorrupted:
      return "Transactions area corrupted";
    case WfsError::kInvalidWfsVersion:
      return "Invalid WFS version";
    case WfsError::kNoSpace:
      return "Not enough free space";
  }
  return "";
}
