/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "entry.h"

#include "directory.h"
#include "file.h"
#include "link.h"
#include "quota_area.h"

Entry::Entry(std::string name, AttributesRef attributes) : name_(std::move(name)), attributes_(std::move(attributes)) {}

Entry::~Entry() = default;

// static
std::expected<std::shared_ptr<Entry>, WfsError> Entry::Load(std::shared_ptr<QuotaArea> quota,
                                                            std::string name,
                                                            AttributesRef attributes_ref) {
  auto* attributes = attributes_ref.get();
  if (attributes->is_link()) {
    // TODO, I think that the link info is in the attributes metadata
    return std::make_shared<Link>(std::move(name), std::move(attributes_ref), std::move(quota));
  } else if (attributes->is_directory()) {
    if (attributes->flags.value() & attributes->Flags::QUOTA) {
      // The directory is quota, aka new area
      auto block_size = BlockSize::Physical;
      if (!(attributes->flags.value() & attributes->Flags::AREA_SIZE_BASIC) &&
          (attributes->flags.value() & attributes->Flags::AREA_SIZE_REGULAR))
        block_size = BlockSize::Logical;
      auto new_quota = quota->LoadQuotaArea(attributes->directory_block_number.value(), block_size);
      if (!new_quota.has_value())
        return std::unexpected(new_quota.error());
      return (*new_quota)->LoadRootDirectory(std::move(name), std::move(attributes_ref));
    } else {
      return quota->LoadDirectory(attributes->directory_block_number.value(), std::move(name),
                                  std::move(attributes_ref));
    }
  } else {
    // IsFile()
    return std::make_shared<File>(std::move(name), std::move(attributes_ref), std::move(quota));
  }
}
