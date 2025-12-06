/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_map.h"

#include <cstring>
#include <numeric>
#include <utility>

#include "quota_area.h"
#include "structs.h"

DirectoryMap::DirectoryMap(std::shared_ptr<QuotaArea> quota, std::shared_ptr<Block> root_block)
    : quota_(std::move(quota)), root_block_(std::move(root_block)) {}

DirectoryMap::iterator DirectoryMap::begin() const {
  auto current_block = root_block_;
  std::vector<iterator::parent_node_info> parents;
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
  std::vector<iterator::parent_node_info> parents;
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

// Or maybe realloc?
Block::DataRef<EntryMetadata> DirectoryMap::alloc_metadata(iterator it, size_t log2_size) {
  auto size = static_cast<uint16_t>(1 << log2_size);
  auto parents = it.parents();
  auto leaf_tree = it.leaf().node;
  auto name = (*it).name;
  while (true) {
    auto new_offset = leaf_tree.Alloc(size);
    if (new_offset.has_value()) {
      return {leaf_tree.block(), *new_offset};
    }
    split_tree(parents, leaf_tree, name);
  }
}

DirectoryMap::iterator DirectoryMap::find(std::string_view key) const {
  auto current_block = root_block_;
  std::vector<iterator::parent_node_info> parents;
  while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
           MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
    parents.push_back({std::move(current_block)});
    parents.back().iterator = parents.back().node.find(key, /*exact_match=*/false);
    assert(!parents.back().iterator.is_end());
    current_block = throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value()));
  }
  iterator::leaf_node_info leaf{{current_block}};
  leaf.iterator = leaf.node.find(key);
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

bool DirectoryMap::insert(std::string_view name, const EntryMetadata* metadata) {
  auto it = find(name);
  if (!it.is_end()) {
    // Already in tree
    return false;
  }
  auto parents = it.parents();
  auto leaf_tree = it.leaf().node;
  while (true) {
    auto size = static_cast<uint16_t>(1 << metadata->metadata_log2_size.value());
    auto new_offset = leaf_tree.Alloc(size);
    if (new_offset.has_value()) {
      Block::RawDataRef<EntryMetadata> new_metadata{leaf_tree.block().get(), *new_offset};
      if (leaf_tree.insert({std::ranges::to<std::string>(name), *new_offset})) {
        std::memcpy(new_metadata.get_mutable(), metadata, size);
        return true;
      }
      leaf_tree.Free(*new_offset, size);
    }
    split_tree(parents, leaf_tree, name);
  }
}

bool DirectoryMap::erase(std::string_view name) {
  auto it = find(name);
  if (it.is_end()) {
    // Not in tree
    return false;
  }
  auto parents = it.parents();
  // Free the entry metadata first
  it.leaf().node.Free((*it.leaf().iterator).value(),
                      static_cast<uint16_t>(1 << (*it).metadata->metadata_log2_size.value()));
  it.leaf().node.erase(it.leaf().iterator);
  bool last_empty = it.leaf().node.empty();
  if (!last_empty)
    return true;
  if (parents.empty()) {
    // Root node empty, reinitialize tree
    assert(it.leaf().node.block() == root_block_);
    Init();
    return true;
  }
  while (true) {
    // Delete child leaf block
    [[maybe_unused]] bool res = quota_->DeleteBlocks((*parents.back().iterator).value(), 1);
    assert(res);
    // Delete child leaf
    if (!parents.back().node.can_erase(parents.back().iterator)) {
      // Erase may fail, split the tree
      auto [parent, parent_it] = parents.back();
      auto parent_key = (*parent_it).key();
      parents.pop_back();
      split_tree(parents, parent, parent_key);
      auto split_point = (*parents.back().iterator).key();
      parents.push_back({throw_if_error(quota_->LoadMetadataBlock((*parents.back().iterator).value()))});
      // We won't find the new parent if it is the first key, because the first key is empty
      if (parent_key == split_point)
        parents.back().iterator = parents.back().node.begin();
      else
        parents.back().iterator = parents.back().node.find(parent_key);
      assert(!parents.back().iterator.is_end());
    }
    parents.back().node.erase(parents.back().iterator);
    last_empty = parents.back().node.empty();
    if (!last_empty)
      return true;
    if (parents.size() == 1) {
      // Root node empty, reinitialize tree
      assert(parents.back().node.block() == root_block_);
      Init();
      return true;
    }
    parents.pop_back();
  }
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
    new_left_block = throw_if_error(quota_->LoadMetadataBlock(
        quota_->to_area_block_number(old_block->physical_block_number()), /*new_block=*/true));
  }
  auto new_right_block = throw_if_error(quota_->AllocMetadataBlock());
  auto new_left_block_number = quota_->to_area_block_number(new_left_block->physical_block_number());
  auto new_right_block_number = quota_->to_area_block_number(new_right_block->physical_block_number());

  TreeType new_left_tree{new_left_block}, new_right_tree{new_right_block};
  auto middle = tree.middle();
  auto middle_key = (*middle).key();
  new_left_tree.Init(/*is_root=*/false);
  new_right_tree.Init(/*is_root=*/false);
  tree.split(new_left_tree, new_right_tree, middle);
  if (old_block == root_block_) {
    root_block_ = throw_if_error(quota_->LoadMetadataBlock(
        quota_->to_area_block_number(root_block_->physical_block_number()), /*new_block=*/true));
    DirectoryParentTree new_root_tree{root_block_};
    new_root_tree.Init(/*is_root=*/true);
    new_root_tree.insert({"", new_left_block_number});
    new_root_tree.insert({middle_key, new_right_block_number});
    parents.emplace_back(new_root_tree);
  } else {
    auto [parent, parent_it] = parents.back();
    parents.pop_back();
    (*parent_it).set_value(new_left_block_number);
    while (!parent.insert({middle_key, new_right_block_number})) {
      split_tree(parents, parent, middle_key);
    }
    parents.emplace_back(parent);
    // TODO: Maybe insert should return iterator so we won't need to find it again?
  }
  parents.back().iterator = parents.back().node.find(middle_key);
  assert(!parents.back().iterator.is_end());

  tree = std::ranges::lexicographical_compare(for_key, middle_key) ? new_left_tree : new_right_tree;
  return true;
}

void DirectoryMap::Init() {
  DirectoryLeafTree root_tree{root_block_};
  root_tree.Init(/*is_root=*/true);
}
