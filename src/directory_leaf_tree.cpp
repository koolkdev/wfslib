/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_leaf_tree.h"

#include <cstring>

void DirectoryLeafTree::Init(bool is_root) {
  base::Init(is_root);
  block()->get_mutable_object<MetadataBlockHeader>(0)->block_flags |= MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE;
}

void DirectoryLeafTree::copy_value(DirectoryTree& new_tree,
                                   parent_node& new_node,
                                   dir_leaf_tree_value_type value) const {
  Block::RawDataRef<EntryMetadata> metadata{block().get(), value};
  auto size = 1 << metadata.get()->metadata_log2_size.value();
  auto new_offset = new_tree.Alloc(static_cast<uint16_t>(size));
  assert(new_offset.has_value());
  Block::RawDataRef<EntryMetadata> new_metadata{new_tree.block().get(), *new_offset};
  std::memcpy(new_metadata.get_mutable(), metadata.get(), size);
  // TODO: Also update current references
  new_node.set_leaf(*new_offset);
}
