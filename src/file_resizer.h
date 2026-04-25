/*
 * Copyright (C) 2026 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <cstddef>

class File;
struct FileLayout;

class FileResizer {
 public:
  static void Resize(File& file, size_t new_size);

 private:
  static void ResizeExternalWithinCurrentAllocation(File& file, size_t new_size);
  static void ResizeInline(File& file, const FileLayout& layout);
};
