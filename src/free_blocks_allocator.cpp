/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_allocator.h"

#include <functional>
#include <ranges>

#include "area.h"
#include "free_blocks_allocator_tree.h"
#include "structs.h"

FreeBlocksAllocator::FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<MetadataBlock> block)
    : area_(std::move(area)), block_(std::move(block)) {}

uint32_t FreeBlocksAllocator::AllocFreeBlockFromCache() {
  if (header()->free_metadata_blocks_count.value() == 0)
    return 0;
  auto* header = mutable_header();
  auto res = header->free_metadata_block.value();
  header->free_metadata_block++;
  header->free_metadata_blocks_count--;
  header->free_blocks_count--;
  return res;
}

uint32_t FreeBlocksAllocator::FindSmallestFreeBlockExtent(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated) {
  for (size_t i = 0; i < kSizeBucketsCount; ++i) {
    FreeBlocksTreeBucket bucket(this, i);
    for (auto it = bucket.find(near); it != bucket.end(); ++it) {
      FreeBlocksExtentInfo possible_result = FreeBlocksExtent{(*it).key, (*it).value, i};
      auto res = std::ranges::find_if(allocated, [&possible_result](FreeBlocksExtentInfo& info) {
        return info.block_number == possible_result.block_number;
      });
      if (res == allocated.end()) {
        possible_result.blocks_count = 1;
        allocated.push_back(possible_result);
        return possible_result.block_number;
      }
      // We already used this extent, let's check if we can use more.
      if (res->blocks_count < possible_result.blocks_count)
        return res->block_number + res->blocks_count++;
    }
  }
  // Not found
  if (near != 0) {
    // try again but all blocks
    return FindSmallestFreeBlockExtent(0, allocated);
  }
  return 0;
}

void FreeBlocksAllocator::AddFreeBlocks(FreeBlocksRangeInfo range) {
  if (!IsRangeIsFree(range)) {
    // Error: part of this range is already free.
    assert(false);
    return;
  }
  AddFreeBlocksForSize(range, kSizeBucketsCount - 1);
  mutable_header()->free_blocks_count += range.blocks_count;
}

void FreeBlocksAllocator::AddFreeBlocksForSize(FreeBlocksRangeInfo range, size_t bucket_index) {
  assert(bucket_index < kSizeBucketsCount);
  uint32_t size_blocks_count = (1 << kSizeBuckets[bucket_index]);
  uint32_t size_blocks_alignment_mask = ~(size_blocks_count - 1);
  uint32_t range_in_size_start = (range.block_number + size_blocks_count - 1) & size_blocks_alignment_mask;
  uint32_t range_in_size_end = range.end_block_number() & size_blocks_alignment_mask;
  if (range_in_size_start >= range_in_size_end) {
    // Doesn't fit in size
    AddFreeBlocksForSize(range, bucket_index - 1);
    return;
  }
  FreeBlocksRangeInfo range_in_size{range_in_size_start, range_in_size_end - range_in_size_start};
  uint32_t next_size_blocks_count = (1 << ((bucket_index + 1 == kSizeBucketsCount) ? (kSizeBuckets[bucket_index] + 4)
                                                                                   : kSizeBuckets[bucket_index + 1]));
  uint32_t next_size_blocks_alignment_mask = ~(next_size_blocks_count - 1);
  FreeBlocksTreeBucket bucket{this, bucket_index};
  std::optional<FreeBlocksExtentInfo> join_before, join_after;
  FreeBlocksTreeBucket::iterator join_before_iter, join_after_iter;
  auto pos = bucket.find(range_in_size.block_number, false);
  auto ftree = pos.ftree();
  if (pos.is_end()) {
    bool start_aligned = !(range_in_size.block_number & next_size_blocks_alignment_mask);
    if ((*pos).end_block_number() == range_in_size.block_number &&
        (!start_aligned || (*pos).blocks_count() + range_in_size.blocks_count < next_size_blocks_count)) {
      join_before = *pos;
      join_before_iter = pos;
      range_in_size.block_number = join_before->block_number;
      range_in_size.blocks_count += join_before->blocks_count;
    }
    ++pos;
    bool end_aligned = !(range_in_size.end_block_number() & next_size_blocks_alignment_mask);
    if (!pos.is_end() && (*pos).block_number() == range_in_size.end_block_number() &&
        (!end_aligned || (*pos).blocks_count() + range_in_size.blocks_count < next_size_blocks_count)) {
      join_after = *pos;
      join_after_iter = pos;
      range_in_size.block_number = join_after->block_number;
      range_in_size.blocks_count += join_after->blocks_count;
    }
  }
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  FreeBlocksRangeInfo sub_range = range_in_size;
  while (sub_range.blocks_count) {
    if (sub_range.blocks_count >= next_size_blocks_count) {
      if (bucket_index == kSizeBucketsCount - 1) {
        // Maximum bucket, use blocks count until alignment)
        auto next_aligned_block_number =
            (sub_range.block_number + next_size_blocks_count) & next_size_blocks_alignment_mask;
        sub_range.blocks_count = next_aligned_block_number - sub_range.block_number;
      } else {
        if (join_before)
          bucket.erase(join_before->block_number, blocks_to_delete);
        if (join_after)
          bucket.erase(join_after->block_number, blocks_to_delete);
        AddFreeBlocks(sub_range);
      }
    } else {
      assert(sub_range.block_number / size_blocks_count <= 0xf);
      auto new_value = static_cast<nibble>(sub_range.block_number / size_blocks_count);
      if (join_before && sub_range.block_number == range_in_size.block_number) {
        (*join_before_iter).value = new_value;
        if (join_after && sub_range.end_block_number() == range_in_size.end_block_number())
          bucket.erase(join_after_iter, blocks_to_delete);
      } else if (join_after && sub_range.end_block_number() == range_in_size.end_block_number()) {
        (*join_after_iter).key = sub_range.block_number;
        (*join_after_iter).value = new_value;
      } else {
        bucket.insert({sub_range.block_number, new_value});
      }
    }
    sub_range.block_number += sub_range.blocks_count;
    sub_range.blocks_count = range_in_size.end_block_number() - sub_range.blocks_count;
  }
  if (range_in_size.block_number > range.block_number)
    AddFreeBlocksForSize({range.block_number, range_in_size.block_number - range.block_number}, bucket_index - 1);
  if (range_in_size.end_block_number() < range.end_block_number())
    AddFreeBlocksForSize(
        {range_in_size.end_block_number(), range.end_block_number() - range_in_size.end_block_number()},
        bucket_index - 1);
  std::ranges::for_each(blocks_to_delete, std::bind(&FreeBlocksAllocator::AddFreeBlocks, this, std::placeholders::_1));
}

bool FreeBlocksAllocator::RemoveFreeBlocksExtent(FreeBlocksExtentInfo extent) {
  // TODO: More aggresive variation
  FreeBlocksTreeBucket bucket{this, extent.bucket_index};
  auto res = bucket.find(extent.block_number, false);
  if (res.is_end())
    return false;
  FreeBlocksExtentInfo full_extent = *res;
  if (full_extent.end_block_number() < extent.end_block_number())
    return false;  // out of extent
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  if (extent.block_number)
    bucket.erase(res, blocks_to_delete);
  std::ranges::for_each(blocks_to_delete, std::bind(&FreeBlocksAllocator::AddFreeBlocks, this, std::placeholders::_1));
  if (!blocks_to_delete.empty()) {
    // We deleted some FTrees, check if the tree is trivial and need to be recreated.
    RecreateEPTreeIfNeeded();
  }
  if (extent.block_number > full_extent.block_number)
    AddFreeBlocks({full_extent.block_number, extent.block_number - full_extent.block_number});
  if (extent.end_block_number() < full_extent.end_block_number())
    AddFreeBlocks({extent.end_block_number(), full_extent.end_block_number() - extent.end_block_number()});
  mutable_header()->free_blocks_count -= extent.blocks_count;
  return true;
}

void FreeBlocksAllocator::RecreateEPTreeIfNeeded() {
  // Check if the EPTree is trivial and can be recreated.
  EPTree eptree{this};
  if (eptree.tree_header()->depth.value() <= 1 && eptree.tree_header()->current_tree.tree_depth.value() == 0) {
    // Already trivial
    return;
  }
  auto last = eptree.rbegin();
  if ((*last).key || (*last).value != 2)
    return;
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  // eptree is empty (aka have only initial FTreee), resize it to one eptree
  for (auto& [node_level, node_it] : std::views::reverse(last.base().nodes())) {
    if (node_level->header() == &eptree.tree_header()->current_tree) {
      // this is the root, reinitialize it
      node_level->Init(1);
      node_level->insert({0, 2});
    } else {
      blocks_to_delete.push_back({node_level->block()->BlockNumber(), 1});
    }
  }
  std::ranges::for_each(blocks_to_delete, std::bind(&FreeBlocksAllocator::AddFreeBlocks, this, std::placeholders::_1));
}

bool FreeBlocksAllocator::IsRangeIsFree(FreeBlocksRangeInfo range) {
  FreeBlocksTree tree{this};
  auto pos = tree.find(range.block_number);
  if (pos.is_end())
    return false;
  // Ensure that previous allocated block is before us
  if ((*pos).end_block_number() > range.block_number)
    return true;
  ++pos;
  // Ensure that previous allocated block is after us
  if (!pos.is_end() && (*pos).block_number() < range.end_block_number())
    return true;
  return false;
}

std::vector<uint32_t> FreeBlocksAllocator::AllocBlocks(uint32_t blocks_count, size_t block_size_index, bool use_cache) {
}

std::vector<FreeBlocksRangeInfo> FreeBlocksAllocator::AllocAreaBlocks(uint32_t blocks_count, size_t block_size_index) {
  using range_and_extents = std::pair<FreeBlocksRangeInfo, std::vector<FreeBlocksExtentInfo>>;
  FreeBlocksTree tree{this};
  std::vector<range_and_extents> ranges;
  std::optional<range_and_extents> selected_range;
  for (auto it = tree.rbegin(); !it.is_end(); ++it) {
    auto extent = *it;
    if (!ranges.empty() && ranges.back().first.block_number == extent.end_block_number()) {
      ranges.back().first.blocks_count += extent.blocks_count();
      ranges.back().first.block_number = extent.block_number();
    } else {
      ranges.push_back({{extent.block_number(), extent.blocks_count()}, {extent}});
    }
    if (ranges.back().first.blocks_count > blocks_count) {
      selected_range = ranges.back();
      break;
    }
  }
  if (selected_range) {
    selected_range->second.back().block_number += selected_range->first.blocks_count - blocks_count;
    selected_range->second.back().blocks_count -= selected_range->first.blocks_count - blocks_count;
    selected_range->first.blocks_count = blocks_count;
    std::ranges::for_each(selected_range->second,
                          std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
    return {selected_range->first};
  }
  std::sort(ranges.begin(), ranges.end(), [](const range_and_extents& a, const range_and_extents& b) {
    return b.first.blocks_count < a.first.blocks_count;
  });
  uint32_t total_blocks;
  std::vector<range_and_extents> used_ranges;
  for (auto& range : ranges) {
    total_blocks += range.first.blocks_count;
    if (total_blocks > blocks_count)
      break;
    used_ranges.push_back(range);
  }
  if (total_blocks < blocks_count || used_ranges.size() > 0x100)
    return {};  // not enough space

  std::sort(ranges.begin(), ranges.end(), [](const range_and_extents& a, const range_and_extents& b) {
    return a.first.block_number < b.first.blocks_count;
  });
  used_ranges.begin()->first.block_number += total_blocks - blocks_count;
  while (used_ranges.begin()->second.back().end_block_number() <= used_ranges.begin()->first.block_number) {
    total_blocks -= used_ranges.begin()->second.back().blocks_count;
    used_ranges.begin()->second.pop_back();
  }
  used_ranges.begin()->second.back().block_number += total_blocks - blocks_count;
  used_ranges.begin()->second.back().blocks_count -= total_blocks - blocks_count;
  for (const auto& range : used_ranges)
    std::ranges::for_each(range.second,
                          std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
  auto res = used_ranges | std::views::transform([](const range_and_extents& x) { return x.first; });
  return {res.begin(), res.end()};
}

std::shared_ptr<MetadataBlock> FreeBlocksAllocator::LoadAllocatorBlock(uint32_t block_number, bool new_block) {
  return throw_if_error(area_->GetMetadataBlock(block_number, new_block));
}

const FreeBlocksAllocatorHeader* FreeBlocksAllocator::header() {
  return block_->get_object<FreeBlocksAllocatorHeader>(sizeof(MetadataBlockHeader));
}
FreeBlocksAllocatorHeader* FreeBlocksAllocator::mutable_header() {
  return block_->get_mutable_object<FreeBlocksAllocatorHeader>(sizeof(MetadataBlockHeader));
}
