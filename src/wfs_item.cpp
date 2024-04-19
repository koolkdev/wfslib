/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "wfs_item.h"

#include "area.h"
#include "directory.h"
#include "file.h"
#include "link.h"

WfsItem::WfsItem(std::string name, AttributesRef attributes)
    : name_(std::move(name)), attributes_(std::move(attributes)) {}

WfsItem::~WfsItem() = default;

// static
std::expected<std::shared_ptr<WfsItem>, WfsError> WfsItem::Load(std::shared_ptr<Area> area,
                                                                std::string name,
                                                                AttributesRef attributes_ref) {
  auto* attributes = attributes_ref.get();
  if (attributes->is_link()) {
    // TODO, I think that the link info is in the attributes metadata
    return std::make_shared<Link>(std::move(name), std::move(attributes_ref), std::move(area));
  } else if (attributes->is_directory()) {
    if (attributes->flags.value() & attributes->Flags::QUOTA) {
      // The directory is quota, aka new area
      auto block_size = Block::BlockSize::Basic;
      if (!(attributes->flags.value() & attributes->Flags::AREA_SIZE_BASIC) &&
          (attributes->flags.value() & attributes->Flags::AREA_SIZE_REGULAR))
        block_size = Block::BlockSize::Regular;
      auto new_area = area->GetArea(attributes->directory_block_number.value(), block_size);
      if (!new_area.has_value())
        return std::unexpected(new_area.error());
      return (*new_area)->LoadRootDirectory(std::move(name), std::move(attributes_ref));
    } else {
      return area->LoadDirectory(attributes->directory_block_number.value(), std::move(name),
                                 std::move(attributes_ref));
    }
  } else {
    // IsFile()
    return std::make_shared<File>(std::move(name), std::move(attributes_ref), std::move(area));
  }
}
