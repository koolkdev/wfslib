/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_tree_bucket.h"

#include <optional>

bool FreeBlocksTreeBucket::insert(FTree::iterator::value_type key_val) {
  auto pos = find_for_insert(key_val.key);
  if (!pos.is_end() && (*pos).key() == key_val.key) {
    // already in tree
    return false;
  }
  return insert(pos, key_val);
}

bool FreeBlocksTreeBucket::insert(iterator& pos, FTree::iterator::value_type key_val) {
  if (pos.ftree().node.insert(pos.ftree().iterator, key_val)) {
    return true;
  }

  auto old_block = pos.ftree().node.block();
  old_block->Detach();
  FTrees old_ftrees{old_block};
  uint32_t left_block_number = (*pos.eptree().iterator).value();
  auto left_block = allocator_->LoadAllocatorBlock(left_block_number, true);
  assert(left_block->physical_block_number() == old_block->physical_block_number());

  std::optional<FreeBlocksExtentInfo> allocated_extent;
  auto right_block_number = allocator_->AllocFreeBlockFromCache();
  if (!right_block_number) {
    // Just get a block from the current FTree
    for (const auto& ftree : old_ftrees.ftrees()) {
      if (ftree.size() > 0) {
        auto first = ftree.begin();
        *allocated_extent = {(*first).key(), 1, ftree.index()};
        break;
      }
    }
    if (!allocated_extent)
      return false;
    right_block_number = allocated_extent->block_number;
  }
  auto right_block = allocator_->LoadAllocatorBlock(right_block_number, true);
  FTreesBlock(left_block).Init();
  FTreesBlock(right_block).Init();

  FTrees left_ftrees{left_block}, right_ftrees{right_block};

  key_type split_point_key;
  old_ftrees.split(left_ftrees, right_ftrees, split_point_key);
  if (key_val.key < split_point_key)
    left_ftrees.ftrees()[block_size_index_].insert(key_val);
  else
    right_ftrees.ftrees()[block_size_index_].insert(key_val);
  if (!pos.eptree().node.insert({split_point_key, right_block_number}))
    return false;
  if (allocated_extent)
    allocator_->RemoveFreeBlocksExtent(*allocated_extent);
  return true;
}

void FreeBlocksTreeBucket::erase(iterator pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
  pos.ftree().node.erase(pos.ftree().iterator);
  // Check for FTrees deletion, we never delete the first FTree (key zero)
  if (!pos.ftree().node.empty() || !(*pos.eptree().iterator).key()) {
    return;
  }
  if (FTrees(pos.ftree().node.block()).empty()) {
    // The FTree is the first block for deletion
    blocks_to_delete.push_back({(*pos.eptree().iterator).value(), 1});
    pos.eptree().node.erase(pos.eptree().iterator, blocks_to_delete);
  }
}

bool FreeBlocksTreeBucket::erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete) {
  auto it = find(key, true);
  if (it == end())
    return false;
  erase(it, blocks_to_delete);
  return true;
}

FreeBlocksTreeBucket::iterator FreeBlocksTreeBucket::begin() const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node.begin();
  assert(!eptree.iterator.is_end());
  iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value()), block_size_index_}};
  ftree.iterator = ftree.node.begin();
  while (ftree.iterator.is_end()) {
    if ((++eptree.iterator).is_end()) {
      // end
      --eptree.iterator;
      return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
    }

    ftree = {{allocator_->LoadAllocatorBlock((*eptree.iterator).value()), block_size_index_}};
    ftree.iterator = ftree.node.begin();
  }
  return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
}

FreeBlocksTreeBucket::iterator FreeBlocksTreeBucket::end() const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node.end();
  assert(!eptree.iterator.is_begin());
  --eptree.iterator;  // EPTree size should always be >= 1
  iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value()), block_size_index_}};
  ftree.iterator = ftree.node.end();
  return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
}

FreeBlocksTreeBucket::iterator FreeBlocksTreeBucket::find(key_type key, bool exact_match) const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node.find(key, false);
  if (exact_match && eptree.iterator.is_end())
    return end();
  iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value()), block_size_index_}};
  ftree.iterator = ftree.node.find(key, exact_match);
  if (!ftree.iterator.is_end() && key >= (*ftree.iterator).key()) {
    return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
  }
  // If FTree is empty or our key is smaller than the first key, go to previous node with value
  if (exact_match)
    return end();
  if (!ftree.iterator.is_begin()) {
    --ftree.iterator;
    return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
  }
  auto orig_eptree_it = eptree.iterator;
  // Go backward to search for value
  while (!eptree.iterator.is_begin()) {
    iterator::ftree_node_info nftree{{allocator_->LoadAllocatorBlock((*--eptree.iterator).value()), block_size_index_}};
    nftree.iterator = nftree.node.end();
    if (!nftree.iterator.is_begin()) {
      --nftree.iterator;
      ftree = std::move(nftree);
      return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
    }
  }
  eptree.iterator = orig_eptree_it;
  // No smaller value, check if we had a value in the original ftree and return it.
  if (!ftree.iterator.is_end()) {
    return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
  }
  // Go forward to search..
  while (!(++eptree.iterator).is_end()) {
    ftree = {{allocator_->LoadAllocatorBlock((*eptree.iterator).value()), block_size_index_}};
    ftree.iterator = ftree.node.begin();
    if (!ftree.iterator.is_end()) {
      // Found value
      return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
    }
  }
  --eptree.iterator;
  return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
}

FreeBlocksTreeBucket::iterator FreeBlocksTreeBucket::find_for_insert(key_type key) const {
  iterator::eptree_node_info eptree{{allocator_}};
  eptree.iterator = eptree.node.find(key, false);
  assert(!eptree.iterator.is_end());
  iterator::ftree_node_info ftree{{allocator_->LoadAllocatorBlock((*eptree.iterator).value()), block_size_index_}};
  ftree.iterator = ftree.node.find(key, false);
  return {allocator_, block_size_index_, std::move(eptree), std::move(ftree)};
}
