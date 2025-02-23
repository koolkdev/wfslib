/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <exception>
#include <expected>

enum WfsError {
  kEntryNotFound,
  kNotDirectory,
  kNotFile,
  kBlockBadHash,
  kAreaHeaderCorrupted,
  kDirectoryCorrupted,
  kFreeBlocksAllocatorCorrupted,
  kFileDataCorrupted,
  kFileMetadataCorrupted,
  kTransactionsAreaCorrupted,
  kInvalidWfsVersion,
  kNoSpace,
};

class WfsException : public std::exception {
 public:
  WfsException(WfsError error) : error_(error) {}
  virtual char const* what() const noexcept override;

  WfsError error() const { return error_; }

 private:
  WfsError error_;
};

template <typename T>
T throw_if_error(std::expected<T, WfsError> res) {
  if (!res.has_value())
    throw WfsException(res.error());
  return *res;
}
