/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "entry.h"

#include <stdexcept>

#include "directory.h"
#include "file.h"
#include "link.h"
#include "quota_area.h"

Entry::Entry(std::string name, MetadataHandlePtr metadata, std::shared_ptr<DirectoryMap> directory_map)
    : name_(std::move(name)), metadata_(std::move(metadata)), directory_map_(std::move(directory_map)) {}

Entry::~Entry() = default;

EntryMetadata* Entry::mutable_metadata() {
  return metadata_->get_mutable();
}

const EntryMetadata* Entry::metadata() const {
  return metadata_->get();
}

const std::shared_ptr<Block>& Entry::metadata_block() const {
  return metadata_->block();
}

// static
std::expected<std::shared_ptr<Entry>, WfsError> Entry::Load(std::shared_ptr<QuotaArea> quota,
                                                            std::string name,
                                                            MetadataHandlePtr metadata_handle,
                                                            std::shared_ptr<DirectoryMap> directory_map) {
  auto* metadata = metadata_handle->get();
  if (metadata->is_link()) {
    // TODO, I think that the link info is in the metadata metadata
    return std::make_shared<Link>(std::move(name), std::move(metadata_handle), std::move(quota),
                                  std::move(directory_map));
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
      return (*new_quota)->LoadRootDirectory(std::move(name), std::move(metadata_handle));
    } else {
      return quota->LoadDirectory(metadata->directory_block_number.value(), std::move(name), std::move(metadata_handle));
    }
  } else {
    // IsFile()
    return std::make_shared<File>(std::move(name), std::move(metadata_handle), std::move(quota),
                                  std::move(directory_map));
  }
}

// static
Entry::MetadataHandlePtr Entry::CreateMetadataHandle(MetadataRef metadata) {
  return std::make_shared<MetadataHandle>(std::move(metadata));
}

uint32_t Entry::owner() const {
  return metadata()->permissions.owner.value();
}
uint32_t Entry::group() const {
  return metadata()->permissions.group.value();
}
uint32_t Entry::mode() const {
  return metadata()->permissions.mode.value();
}
uint32_t Entry::creation_time() const {
  return metadata()->ctime.value();
}
uint32_t Entry::modification_time() const {
  return metadata()->mtime.value();
}

Entry::MetadataHandle::MetadataHandle(MetadataRef metadata) : metadata_(std::move(metadata)) {}

const EntryMetadata* Entry::MetadataHandle::get() const {
  if (!metadata_.has_value())
    throw std::logic_error("Entry metadata handle is no longer attached to a directory entry");
  return metadata_->get();
}

EntryMetadata* Entry::MetadataHandle::get_mutable() const {
  if (!metadata_.has_value())
    throw std::logic_error("Entry metadata handle is no longer attached to a directory entry");
  return metadata_->get_mutable();
}

const std::shared_ptr<Block>& Entry::MetadataHandle::block() const {
  if (!metadata_.has_value())
    throw std::logic_error("Entry metadata handle is no longer attached to a directory entry");
  return metadata_->block;
}

void Entry::MetadataHandle::Update(MetadataRef metadata) {
  metadata_ = std::move(metadata);
}

void Entry::MetadataHandle::Invalidate() {
  metadata_.reset();
}
