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

Entry::Entry(std::string name, MetadataRef metadata) : name_(std::move(name)), metadata_(std::move(metadata)) {}

Entry::~Entry() = default;

// static
std::expected<std::shared_ptr<Entry>, WfsError> Entry::Load(std::shared_ptr<QuotaArea> quota,
                                                            std::string name,
                                                            MetadataRef metadata_ref) {
  auto* metadata = metadata_ref.get();
  if (metadata->is_link()) {
    // TODO, I think that the link info is in the metadata metadata
    return std::make_shared<Link>(std::move(name), std::move(metadata_ref), std::move(quota));
  } else if (metadata->is_directory()) {
    if (metadata->flags.value() & metadata->Flags::QUOTA) {
      // The directory is quota, aka new area
      auto block_size = BlockSize::Physical;
      if (!(metadata->flags.value() & metadata->Flags::AREA_SIZE_BASIC) &&
          (metadata->flags.value() & metadata->Flags::AREA_SIZE_REGULAR))
        block_size = BlockSize::Logical;
      auto new_quota = quota->LoadQuotaArea(metadata->directory_block_number.value(), block_size);
      if (!new_quota.has_value())
        return std::unexpected(new_quota.error());
      return (*new_quota)->LoadRootDirectory(std::move(name), std::move(metadata_ref));
    } else {
      return quota->LoadDirectory(metadata->directory_block_number.value(), std::move(name), std::move(metadata_ref));
    }
  } else {
    // IsFile()
    return std::make_shared<File>(std::move(name), std::move(metadata_ref), std::move(quota));
  }
}

uint32_t Entry::owner() const { return metadata()->permissions.owner.value(); }
uint32_t Entry::group() const { return metadata()->permissions.group.value(); }
uint32_t Entry::mode() const { return metadata()->permissions.mode.value(); }
uint32_t Entry::creation_time() const { return metadata()->ctime.value(); }
uint32_t Entry::modification_time() const { return metadata()->mtime.value(); }
