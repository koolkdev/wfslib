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

namespace {

inline uint32_t align_floor_pow2(uint32_t block_number, size_t pow2) {
  return block_number & ~((1 << pow2) - 1);
}

inline uint32_t align_ceil_pow2(uint32_t block_number, size_t pow2) {
  return (block_number + (1 << pow2) - 1) & ~((1 << pow2) - 1);
}

inline uint32_t is_aligned_pow2(uint32_t block_number, size_t pow2) {
  return !(block_number & ((1 << pow2) - 1));
}

size_t BlockSizeToIndex(Block::BlockSizeType size) {
  return size / 3;
}

}  // namespace

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
      FreeBlocksExtentInfo possible_result = *it;
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
    // TODO: Search backward
  }
  // Not found
  return 0;
}

bool FreeBlocksAllocator::AddFreeBlocks(FreeBlocksRangeInfo range) {
  if (!IsRangeIsFree(range)) {
    // Error: part of this range is already free.
    assert(false);
    return false;
  }
  AddFreeBlocksForSize(range, kSizeBucketsCount - 1);
  mutable_header()->free_blocks_count += range.blocks_count;
  return true;
}

void FreeBlocksAllocator::AddFreeBlocksForSize(FreeBlocksRangeInfo range, size_t bucket_index) {
  assert(bucket_index < kSizeBucketsCount);
  const uint32_t size_blocks_count = 1 << kSizeBuckets[bucket_index];
  uint32_t range_in_size_start = align_ceil_pow2(range.block_number, kSizeBuckets[bucket_index]);
  uint32_t range_in_size_end = align_floor_pow2(range.end_block_number(), kSizeBuckets[bucket_index]);
  if (range_in_size_start >= range_in_size_end) {
    // Doesn't fit in size
    AddFreeBlocksForSize(range, bucket_index - 1);
    return;
  }
  FreeBlocksRangeInfo range_in_size{range_in_size_start, range_in_size_end - range_in_size_start};
  size_t next_size_pow2 =
      (bucket_index + 1 == kSizeBucketsCount) ? (kSizeBuckets[bucket_index] + 4) : kSizeBuckets[bucket_index + 1];
  uint32_t next_size_blocks_count = 1 << next_size_pow2;
  FreeBlocksTreeBucket bucket{this, bucket_index};
  std::optional<FreeBlocksExtentInfo> join_before, join_after;
  FreeBlocksTreeBucket::iterator join_before_iter, join_after_iter;
  auto pos = bucket.find(range_in_size.block_number, false);
  auto ftree = pos.ftree();
  if (pos.is_end()) {
    // Join with prev if:
    // 1. It ends exactly at this range start.
    // 2. This range start isn't aligned to one size up bucket block.
    // 3. OR joining those ranges will still be smaller than one size up bucket
    if ((*pos).end_block_number() == range_in_size.block_number &&
        (!is_aligned_pow2(range_in_size.block_number, next_size_pow2) ||
         (*pos).blocks_count() + range_in_size.blocks_count < next_size_blocks_count)) {
      join_before = *pos;
      join_before_iter = pos;
      range_in_size.block_number = join_before->block_number;
      range_in_size.blocks_count += join_before->blocks_count;
    }
    // Join with next if:
    // 1. It start  exactly at this range end.
    // 2. This range end isn't aligned to one size up bucket block.
    // 3. OR joining those ranges will still be smaller than one size up bucket
    ++pos;
    if (!pos.is_end() && (*pos).block_number() == range_in_size.end_block_number() &&
        (!is_aligned_pow2(range_in_size.end_block_number(), next_size_pow2) ||
         (*pos).blocks_count() + range_in_size.blocks_count < next_size_blocks_count)) {
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
        sub_range.blocks_count = align_ceil_pow2(sub_range.block_number, next_size_pow2) - sub_range.block_number;
      } else {
        if (join_before)
          bucket.erase(join_before->block_number, blocks_to_delete);
        if (join_after)
          bucket.erase(join_after->block_number, blocks_to_delete);
        AddFreeBlocks(sub_range);
      }
    } else {
      assert(sub_range.blocks_count > 0 && sub_range.blocks_count % size_blocks_count == 0 &&
             sub_range.blocks_count / size_blocks_count <= 0x10);
      auto new_value = static_cast<nibble>(sub_range.blocks_count / size_blocks_count - 1);
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
  auto nodes = last.base().nodes();
  for (auto& [node_level, node_it] : std::views::reverse(nodes)) {
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

std::optional<std::vector<uint32_t>> FreeBlocksAllocator::AllocBlocks(uint32_t chunks_count,
                                                                      Block::BlockSizeType size,
                                                                      bool use_cache) {
  std::vector<uint32_t> result;
  size_t size_index = BlockSizeToIndex(size);
  uint32_t need_more_blocks_count = chunks_count << size;
  if (!need_more_blocks_count)
    return result;
  if (use_cache && size_index == 0 && area_->BlocksCacheSize()) {
    do {
      auto blocks_from_cache = std::min(need_more_blocks_count, header()->free_metadata_blocks_count.value());
      auto* header = mutable_header();
      auto cache_block_number = header->free_metadata_block.value();
      header->free_metadata_block += blocks_from_cache;
      header->free_metadata_blocks_count -= blocks_from_cache;
      header->free_blocks_count -= blocks_from_cache;
      need_more_blocks_count -= blocks_from_cache;
      std::ranges::copy(std::views::iota(cache_block_number, cache_block_number + blocks_from_cache),
                        std::back_inserter(result));
      if (!need_more_blocks_count)
        return result;
    } while (ReplanishBlocksCache());
  }
  if (size_index == 0) {
    if (AllocBlocksOfSpecificSize(need_more_blocks_count, size_index, 0, result))
      return result;
  }
  if (size_index <= 1) {
    if (AllocBlocksOfSpecificSize(need_more_blocks_count, size_index, 1, result))
      return result;
  }
  if (AllocBlocksOfSpecificSize(need_more_blocks_count, size_index, kSizeBucketsCount, result))
    return result;
  // Not enough free blocks
  return std::nullopt;
}

bool FreeBlocksAllocator::ReplanishBlocksCache() {
  uint32_t blocks_to_alloc = 1 << area_->BlocksCacheSize();
  std::optional<FreeBlocksExtentInfo> selected_extent;
  for (size_t i = 0; i < kSizeBucketsCount; ++i) {
    FreeBlocksTreeBucket bucket(this, i);
    auto it = bucket.begin();
    if (!it.is_end())
      continue;
    FreeBlocksExtentInfo extent = *it;
    if (extent.blocks_count >= blocks_to_alloc * 2 &&
        (!selected_extent || extent.block_number < selected_extent->block_number))
      selected_extent = extent;
  }
  if (!selected_extent)
    return false;
  // Round the block number to be aligned to blocks cache in the whole disk
  auto abs_block_number = area_->AbsoluteBlockNumber(selected_extent->block_number);
  auto aligned_block_number = align_ceil_pow2(
      abs_block_number, area_->BlocksCacheSize() + area_->header()->log2_block_size.value() - Block::BlockSize::Basic);
  selected_extent->block_number = area_->RelativeBlockNumber(aligned_block_number);
  selected_extent->blocks_count = blocks_to_alloc;
  return true;
}

bool FreeBlocksAllocator::AllocBlocksOfSpecificSize(uint32_t blocks_count,
                                                    size_t size_index,
                                                    size_t max_size_index,
                                                    std::vector<uint32_t>& result) {
  FreeBlocksTree tree{this};
  std::vector<FreeBlocksExtentInfo> extents;
  for (const auto& tree_extent : tree) {
    FreeBlocksExtentInfo extent = tree_extent;
    if (extent.bucket_index < size_index || extent.bucket_index > max_size_index)
      continue;
    if (extent.blocks_count > blocks_count) {
      extent.blocks_count = blocks_count;
    }
    blocks_count -= extent.blocks_count;
    extents.push_back(extent);
    for (uint32_t i = 0; i < extent.blocks_count >> (kSizeBuckets[extent.bucket_index] - kSizeBuckets[size_index]); ++i)
      result.push_back(extent.block_number + (i << kSizeBuckets[size_index]));
  }
  if (blocks_count) {
    // Not enough free blocks under those conditions
    return false;
  }
  std::ranges::for_each(extents, std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
  return true;
}

std::optional<std::vector<FreeBlocksRangeInfo>> FreeBlocksAllocator::AllocAreaBlocks(uint32_t chunks_count,
                                                                                     Block::BlockSizeType size) {
  using range_and_extents = std::pair<FreeBlocksRangeInfo, std::vector<FreeBlocksExtentInfo>>;
  size_t size_index = BlockSizeToIndex(size);
  uint32_t wanted_blocks_count = chunks_count << size;
  FreeBlocksTree tree{this};
  std::vector<range_and_extents> ranges;
  std::optional<range_and_extents> selected_range;
  for (auto it = tree.rbegin(); !it.is_end(); ++it) {
    auto extent = *it;
    if (extent.bucket_index < size_index)
      continue;
    if (!ranges.empty() && ranges.back().first.block_number == extent.end_block_number()) {
      ranges.back().first.blocks_count += extent.blocks_count();
      ranges.back().first.block_number = extent.block_number();
    } else {
      ranges.push_back({{extent.block_number(), extent.blocks_count()}, {extent}});
    }
    if (ranges.back().first.blocks_count > wanted_blocks_count) {
      selected_range = ranges.back();
      break;
    }
  }
  if (selected_range) {
    // remove  unneeded blocks
    selected_range->second.back().block_number += selected_range->first.blocks_count - wanted_blocks_count;
    selected_range->second.back().blocks_count -= selected_range->first.blocks_count - wanted_blocks_count;
    selected_range->first.blocks_count = wanted_blocks_count;
    std::ranges::for_each(selected_range->second,
                          std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
    return std::vector<FreeBlocksRangeInfo>{selected_range->first};
  }
  // use the bigger chunkis first
  std::sort(ranges.begin(), ranges.end(), [](const range_and_extents& a, const range_and_extents& b) {
    return b.first.blocks_count < a.first.blocks_count;
  });
  uint32_t total_blocks{0};
  std::vector<range_and_extents> used_ranges;
  for (auto& range : ranges) {
    total_blocks += range.first.blocks_count;
    if (total_blocks > wanted_blocks_count)
      break;
    used_ranges.push_back(range);
  }
  if (total_blocks < wanted_blocks_count || used_ranges.size() > 0x100)
    return std::nullopt;  // not enough space
  // now sort by block number
  std::sort(ranges.begin(), ranges.end(), [](const range_and_extents& a, const range_and_extents& b) {
    return a.first.block_number < b.first.blocks_count;
  });
  // remove from first chunk unneeded blocks
  used_ranges.begin()->first.block_number += total_blocks - wanted_blocks_count;
  while (used_ranges.begin()->second.back().end_block_number() <= used_ranges.begin()->first.block_number) {
    total_blocks -= used_ranges.begin()->second.back().blocks_count;
    used_ranges.begin()->second.pop_back();
  }
  used_ranges.begin()->second.back().block_number += total_blocks - wanted_blocks_count;
  used_ranges.begin()->second.back().blocks_count -= total_blocks - wanted_blocks_count;
  for (const auto& range : used_ranges)
    std::ranges::for_each(range.second,
                          std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
  auto res = used_ranges | std::views::transform([](const range_and_extents& x) { return x.first; });
  return std::vector<FreeBlocksRangeInfo>{res.begin(), res.end()};
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
