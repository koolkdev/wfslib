/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_map.h"

#include <numeric>
#include <utility>

#include "quota_area.h"
#include "structs.h"

DirectoryMap::DirectoryMap(std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> root_block)
    : quota_(std::move(quota)), root_block_(std::move(root_block)) {}

DirectoryMap::iterator DirectoryMap::begin() const {
  auto current_block = root_block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.begin();
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value()));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node.begin();
  return {quota_, std::move(parents), std::move(leaf)};
}

DirectoryMap::iterator DirectoryMap::end() const {
  auto current_block = root_block_;
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.end();
    assert(!parents.back().iterator.is_begin());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*--parents.back().iterator).value()));
  }
  iterator::leaf_node_info leaf{std::move(current_block)};
  leaf.iterator = leaf.node.end();
  return {quota_, std::move(parents), std::move(leaf)};
}

DirectoryMap::iterator DirectoryMap::find(std::string_view key, bool exact_match) const {
  auto [parents, leaf_tree] = find_leaf_tree(key);
  iterator::leaf_node_info leaf{std::move(leaf_tree)};
  leaf.iterator = leaf.node.find(key, exact_match);
  return {quota_, std::move(parents), std::move(leaf)};
}

size_t DirectoryMap::CalcSizeOfDirectoryBlock(std::shared_ptr<Block> block) const {
  if (block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
      MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE) {
    return DirectoryLeafTree{std::move(block)}.size();
  } else {
    DirectoryParentTree tree{std::move(block)};
    return std::accumulate(tree.begin(), tree.end(), size_t{0}, [&](auto acc, const auto& node) {
      return acc + CalcSizeOfDirectoryBlock(throw_if_error(quota_->LoadMetadataBlock(node.value())));
    });
  }
};

bool DirectoryMap::insert(std::string_view name, const Attributes* attributes) {
  auto [nparents, leaf_tree] = find_leaf_tree(name);
  auto parents = nparents | std::ranges::to<std::vector>();
  if (!leaf_tree.find(name).is_end()) {
    // Already in tree
    return false;
  }
  while (true) {
    auto size = static_cast<uint16_t>(1 << attributes->entry_log2_size.value());
    auto new_offset = leaf_tree.Alloc(size);
    if (new_offset) {
      Block::RawDataRef<Attributes> new_attributes{leaf_tree.block().get(), new_offset};
      if (leaf_tree.insert({name | std::ranges::to<std::string>(), new_offset})) {
        std::memcpy(new_attributes.get_mutable(), attributes, size);
        return true;
      }
      leaf_tree.Free(new_offset, size);
    }
    split_tree(parents, leaf_tree, name);
  }
}

std::pair<std::deque<DirectoryMap::iterator::parent_node_info>, DirectoryLeafTree> DirectoryMap::find_leaf_tree(
    std::string_view key) const {
  auto current_block = root_block_;
  if (size() == 0)
    return {{}, {std::move(current_block)}};
  std::deque<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.find(key, /*exact_match=*/false);
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value()));
  }
  return {parents, {std::move(current_block)}};
}

template <DirectoryTreeImpl TreeType>
bool DirectoryMap::split_tree(std::vector<iterator::parent_node_info>& parents,
                              TreeType& tree,
                              std::string_view for_key) {
  auto old_block = tree.block();
  old_block->Detach();
  std::shared_ptr<Block> new_left_block;
  // TODO: What happens if no space for new block? currently there is an exception.
  if (old_block == root_block_) {
    new_left_block = throw_if_error(quota_->AllocMetadataBlock());
  } else {
    new_left_block = throw_if_error(
        quota_->LoadMetadataBlock(quota_->to_area_block_number(old_block->device_block_number()), /*new_block=*/true));
  }
  auto new_right_block = throw_if_error(quota_->AllocMetadataBlock());
  auto new_left_block_number = quota_->to_area_block_number(new_left_block->device_block_number());
  auto new_right_block_number = quota_->to_area_block_number(new_right_block->device_block_number());

  TreeType new_left_tree{new_left_block}, new_right_tree{new_right_block};
  auto middle = tree.middle();
  auto middle_key = (*middle).key();
  tree.split(new_left_tree, new_right_tree, middle);
  if (old_block == root_block_) {
    root_block_ = throw_if_error(quota_->LoadMetadataBlock(
        quota_->to_area_block_number(root_block_->device_block_number()), /*new_block=*/true));
    DirectoryParentTree new_root_tree{root_block_};
    new_root_tree.Init();
    new_root_tree.insert({"", new_left_block_number});
    new_root_tree.insert({middle_key, new_right_block_number});
    parents.emplace_back(new_root_tree);
  } else {
    auto parent = parents.back();
    parents.pop_back();
    (*parent.iterator).set_value(new_left_block_number);
    while (!parent.node.insert({middle_key, new_right_block_number})) {
      split_tree(parents, parent.node, middle_key);
    }
    parents.push_back(parent);
    // TODO: Maybe insert should return iterator so we won't need to find it again?
  }
  parents.back().iterator = parents.back().node.find(middle_key);

  tree = std::ranges::lexicographical_compare(for_key, middle_key) ? new_left_tree : new_right_tree;
  return true;
}
