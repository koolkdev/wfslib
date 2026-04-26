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

Entry::Entry(EntryHandlePtr handle) : handle_(std::move(handle)) {}

Entry::~Entry() = default;

std::string Entry::name() const {
  if (!handle_->directory_map())
    return std::string{handle_->key()};
  return handle_->get()->GetCaseSensitiveName(handle_->key());
}

EntryMetadata* Entry::mutable_metadata() {
  return handle_->get_mutable();
}

const EntryMetadata* Entry::metadata() const {
  return handle_->get();
}

const std::shared_ptr<Block>& Entry::metadata_block() const {
  return handle_->block();
}

// static
std::expected<std::shared_ptr<Entry>, WfsError> Entry::Load(std::shared_ptr<QuotaArea> quota, EntryHandlePtr handle) {
  auto* metadata = handle->get();
  if (metadata->is_link()) {
    // TODO, I think that the link info is in the metadata metadata
    return std::make_shared<Link>(std::move(handle), std::move(quota));
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
      return (*new_quota)->LoadRootDirectory(std::move(handle));
    } else {
      return quota->LoadDirectory(metadata->directory_block_number.value(), std::move(handle));
    }
  } else {
    // IsFile()
    return std::make_shared<File>(std::move(handle), std::move(quota));
  }
}

// static
Entry::EntryHandlePtr Entry::CreateEntryHandle(std::shared_ptr<DirectoryMap> directory_map,
                                               std::string key,
                                               MetadataRef metadata) {
  return std::make_shared<EntryHandle>(std::move(directory_map), std::move(key), std::move(metadata));
}

// static
Entry::EntryHandlePtr Entry::CreateSyntheticEntryHandle(std::string name, MetadataRef metadata) {
  return std::make_shared<EntryHandle>(nullptr, std::move(name), std::move(metadata));
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

Entry::EntryHandle::EntryHandle(std::shared_ptr<DirectoryMap> directory_map, std::string key, MetadataRef metadata)
    : directory_map_(std::move(directory_map)), key_(std::move(key)), metadata_(std::move(metadata)) {}

std::string_view Entry::EntryHandle::key() const {
  return key_;
}

const std::shared_ptr<DirectoryMap>& Entry::EntryHandle::directory_map() const {
  return directory_map_;
}

const EntryMetadata* Entry::EntryHandle::get() const {
  return metadata_ref().get();
}

EntryMetadata* Entry::EntryHandle::get_mutable() const {
  return metadata_ref().get_mutable();
}

const std::shared_ptr<Block>& Entry::EntryHandle::block() const {
  return metadata_ref().block;
}

void Entry::EntryHandle::Update(std::string key, MetadataRef metadata) {
  key_ = std::move(key);
  metadata_ = std::move(metadata);
}

void Entry::EntryHandle::Invalidate() {
  metadata_.reset();
}

const Entry::MetadataRef& Entry::EntryHandle::metadata_ref() const {
  if (!metadata_.has_value())
    throw std::logic_error("Invalid directory entry handle");
  return *metadata_;
}
