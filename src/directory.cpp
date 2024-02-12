/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory.h"

#include "area.h"
#include "file.h"
#include "link.h"
#include "metadata_block.h"
#include "structs.h"
#include "sub_block_allocator.h"

using NodeState = DirectoryItemsIterator::NodeState;
using DirectoryTree = SubBlockAllocator<DirectoryTreeHeader>;

std::shared_ptr<WfsItem> Directory::GetObject(const std::string& name) const {
  AttributesBlock attributes_block = GetObjectAttributes(block_, name);
  if (!attributes_block)
    return nullptr;
  return Create(name, attributes_block);
}

std::shared_ptr<Directory> Directory::GetDirectory(const std::string& name) const {
  AttributesBlock attributes_block = GetObjectAttributes(block_, name);
  if (!attributes_block) {
    // Not found
    return nullptr;
  }
  auto attributes = attributes_block.Attributes();
  if (attributes->IsLink() || !attributes->IsDirectory()) {
    // Not a directory
    return nullptr;
  }
  return std::dynamic_pointer_cast<Directory>(Create(name, attributes_block));
}

std::shared_ptr<File> Directory::GetFile(const std::string& name) const {
  AttributesBlock attributes_block = GetObjectAttributes(block_, name);
  auto attributes = attributes_block.Attributes();
  if (!attributes_block) {
    // Not found
    return nullptr;
  }
  if (attributes->IsLink() || attributes->IsDirectory()) {
    // Not a file
    return nullptr;
  }
  return std::dynamic_pointer_cast<File>(Create(name, attributes_block));
}

std::shared_ptr<WfsItem> Directory::Create(const std::string& name, const AttributesBlock& attributes_block) const {
  auto attributes = attributes_block.Attributes();
  if (attributes->IsLink()) {
    // TODO, I think that the link info is in the attributes metadata
    return std::make_shared<Link>(name, attributes_block, area_);
  } else if (attributes->IsDirectory()) {
    if (attributes->flags.value() & attributes->Flags::QUOTA) {
      // The directory is quota, aka new area
      auto block_size = Block::BlockSize::Basic;
      if (!(attributes->flags.value() & attributes->Flags::AREA_SIZE_BASIC) &&
          (attributes->flags.value() & attributes->Flags::AREA_SIZE_REGULAR))
        block_size = Block::BlockSize::Regular;
      auto new_area = area_->GetArea(attributes->directory_block_number.value(), name, attributes_block, block_size);
      return new_area->GetRootDirectory();
    } else {
      return area_->GetDirectory(attributes->directory_block_number.value(), name, attributes_block);
    }
  } else {
    // IsFile()
    return std::make_shared<File>(name, attributes_block, area_);
  }
}

AttributesBlock Directory::GetObjectAttributes(const std::shared_ptr<MetadataBlock>& block,
                                               const std::string& name) const {
  DirectoryTree dir_tree{block};
  auto current_node =
      as_const(block.get())->get_object<DirectoryTreeNode>(std::as_const(dir_tree).extra_header()->root.value());
  if (block->Header()->block_flags.value() & block->Header()->Flags::EXTERNAL_DIRECTORY_TREE) {
    auto pos_in_path = name.begin();
    while (true) {
      auto external_node = static_cast<const ExternalDirectoryTreeNode*>(current_node);
      if (current_node->prefix_length.value() > static_cast<size_t>(std::distance(pos_in_path, name.end()))) {
        // not equal.. path too long
        return {};
      }
      if (current_node->prefix_length.value() &&
          std::strncmp(&*pos_in_path, current_node->prefix().data(), current_node->prefix_length.value())) {
        // not equal.. not found
        return {};
      }
      pos_in_path += current_node->prefix_length.value();
      char next_expected_char = 0;
      if (pos_in_path < name.end())
        next_expected_char = *pos_in_path;
      auto choices = current_node->choices();
      // This is sorted list, so we can find it with lower_bound
      auto res = std::lower_bound(choices.begin(), choices.end(), std::byte{(uint8_t)next_expected_char});
      if (res == choices.end() || std::to_integer<char>(*res) != next_expected_char) {
        // Not found
        return {};
      }
      auto value_offset = external_node->get_item(res - choices.begin()).value();
      if (pos_in_path == name.end()) {
        // We found the attribute!
        return {block, value_offset};
      }
      pos_in_path++;
      // Go to next node
      current_node = block->get_object<DirectoryTreeNode>(value_offset);
    }
  } else {
    // Arghh, trees over trees
    auto node_state = std::make_shared<NodeState>(NodeState{block, current_node, nullptr, 0, current_node->prefix()});
    // -- because it will be advanced immedialty to 0 when we do ++
    --node_state->current_index;
    uint32_t last_block_number = 0;
    while (true) {
      if (++(node_state->current_index) == node_state->node->choices_count.value()) {
        // Got to the end of the node, go up
        node_state = std::move(node_state->parent);
        if (!node_state)
          break;
        continue;
      }
      // Enter nodes until we hit a node that has value
      while (node_state->node->choices()[node_state->current_index] != std::byte{0}) {
        auto node_block = node_state->block;
        uint16_t node_offset = 0;
        node_offset = static_cast<const InternalDirectoryTreeNode*>(node_state->node)
                          ->get_item(node_state->current_index)
                          .value();
        current_node = block->get_object<DirectoryTreeNode>(node_offset);
        std::string path =
            node_state->path +
            std::string(1, std::to_integer<char>(node_state->node->choices()[node_state->current_index])) +
            current_node->prefix();
        node_state = std::make_shared<NodeState>(NodeState{node_block, current_node, std::move(node_state), 0, path});
      }
      // Check if our string is lexicographic smaller
      if (node_state->path.size() &&
          std::strncmp(&*name.begin(), &*node_state->path.begin(), std::min(name.size(), node_state->path.size())) < 0)
        break;
      last_block_number =
          static_cast<const InternalDirectoryTreeNode*>(node_state->node)->get_next_allocator_block_number().value();
      if (node_state->path == name)
        break;  // No need to continue with the search
    }
    if (!last_block_number) {
      // Not found
      return {};
    }
    return GetObjectAttributes(area_->GetMetadataBlock(last_block_number), name);
  }
}

size_t Directory::Size() const {
  return std::distance(begin(), end());
}

DirectoryItemsIterator Directory::begin() const {
  DirectoryTree dir_tree{block_};
  auto current_node =
      as_const(block_.get())->get_object<DirectoryTreeNode>(std::as_const(dir_tree).extra_header()->root.value());
  if (!current_node->choices_count.value())
    return end();
  auto node_state = std::make_shared<NodeState>(NodeState{block_, current_node, nullptr, 0, current_node->prefix()});
  // -- because it will be advanced immedialty to 0 when we do ++
  --node_state->current_index;
  auto res = DirectoryItemsIterator(shared_from_this(), std::move(node_state));
  ++res;
  return res;
}

DirectoryItemsIterator Directory::end() const {
  return DirectoryItemsIterator(shared_from_this(), nullptr);
}
