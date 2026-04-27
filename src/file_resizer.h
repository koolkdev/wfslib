/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "file.h"
#include "file_layout.h"

class EntryMetadataReplacement {
 public:
  EntryMetadataReplacement(const EntryMetadata* source, const FileLayout& layout);

  EntryMetadata* get();
  const EntryMetadata* get() const;

 private:
  std::vector<std::byte> storage_;
};

class FileResizer {
 public:
  explicit FileResizer(std::shared_ptr<File> file);

  void Resize(size_t new_size);

 private:
  void ResizeInline(const FileLayout& target_layout);
  void ReplaceMetadata(EntryMetadata* metadata);

  template <FileLayoutCategory Category>
  void ResizeDataUnitLayout(const FileLayout& target_layout);

  std::shared_ptr<File> file_;
};
