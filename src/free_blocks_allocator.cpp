/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "free_blocks_allocator.h"

#include <algorithm>
#include <functional>
#include <ranges>

#include "area.h"
#include "free_blocks_tree.h"
#include "free_blocks_tree_bucket.h"
#include "structs.h"

static_assert(std::ranges::equal(kSizeBuckets, std::to_array({0, 3, 6, 10, 14, 18, 22})));

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

FreeBlocksAllocator::FreeBlocksAllocator(std::shared_ptr<Area> area, std::shared_ptr<Block> block)
    : area_(std::move(area)), block_(std::move(block)) {}

void FreeBlocksAllocator::Init(std::vector<FreeBlocksRangeInfo> initial_free_blocks) {
  // Init cach info
  auto* header = mutable_header();
  header->free_blocks_count = 0;
  header->always_one = 1;

  if (BlocksCacheSizeLog2()) {
    uint32_t cache_end_block_number = initial_free_blocks[0].block_number + (1 << BlocksCacheSizeLog2());
    cache_end_block_number = area_->to_device_block_number(cache_end_block_number);
    cache_end_block_number = align_ceil_pow2(
        cache_end_block_number, BlocksCacheSizeLog2() + area_->block_size_log2() - Block::BlockSize::Basic);
    cache_end_block_number = area_->to_area_block_number(cache_end_block_number);
    uint32_t cache_free_blocks_count = cache_end_block_number - initial_free_blocks[0].block_number;
    if (cache_free_blocks_count > initial_free_blocks[0].blocks_count) {
      cache_free_blocks_count = initial_free_blocks[0].blocks_count;
    }
    header->free_blocks_cache = initial_free_blocks[0].block_number;
    header->free_blocks_cache_count = cache_free_blocks_count;
    header->free_blocks_count += cache_free_blocks_count;

    initial_free_blocks[0].block_number += cache_free_blocks_count;
    initial_free_blocks[0].blocks_count -= cache_free_blocks_count;
  } else {
    header->free_blocks_cache = 0;
    header->free_blocks_cache_count = 0;
  }

  // TODO: support sparse areas
  EPTree eptree{this};
  eptree.Init(area_->to_area_block_number(block_->device_block_number()));
  // TODO: 2 -> k
  auto ftree_block = LoadAllocatorBlock(2, true);
  FTrees ftrees{std::move(ftree_block)};
  ftrees.Init();
  eptree.insert({0, 2});
  for (const auto& free_range : initial_free_blocks)
    AddFreeBlocks(free_range);
}

uint32_t FreeBlocksAllocator::AllocFreeBlockFromCache() {
  if (header()->free_blocks_cache_count.value() == 0)
    return 0;
  auto* header = mutable_header();
  auto res = header->free_blocks_cache.value();
  header->free_blocks_cache++;
  header->free_blocks_cache_count--;
  header->free_blocks_count--;
  return res;
}

uint32_t FreeBlocksAllocator::FindSmallestFreeBlockExtent(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated) {
  // Since we are releasing everything only at the end we need to track the allocated blocks so far which makes this
  // logic bit more complex than original.
  for (size_t i = 0; i < kSizeBuckets.size(); ++i) {
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
  if (range.blocks_count == 0 || IsRangeIsFree(range)) {
    // Error: part of this range is already free.
    assert(false);
    return false;
  }
  AddFreeBlocksForSize(range, kSizeBuckets.size() - 1);
  mutable_header()->free_blocks_count += range.blocks_count;
  return true;
}

void FreeBlocksAllocator::AddFreeBlocksForSize(FreeBlocksRangeInfo range, size_t bucket_index) {
  assert(bucket_index < kSizeBuckets.size());
  assert(range.blocks_count > 0);
  const uint32_t size_blocks_count = 1 << kSizeBuckets[bucket_index];
  if (range.blocks_count < size_blocks_count) {
    AddFreeBlocksForSize(range, bucket_index - 1);
    return;
  }
  uint32_t range_in_size_start = align_ceil_pow2(range.block_number, kSizeBuckets[bucket_index]);
  uint32_t range_in_size_end = align_floor_pow2(range.end_block_number(), kSizeBuckets[bucket_index]);
  if (range_in_size_start >= range_in_size_end) {
    assert(range_in_size_start == range_in_size_end);
    // Doesn't fit in size
    if (range.block_number < range_in_size_start) {
      AddFreeBlocksForSize({range.block_number, range_in_size_start - range.block_number}, bucket_index - 1);
    }
    if (range.end_block_number() > range_in_size_end) {
      AddFreeBlocksForSize({range_in_size_end, range.end_block_number() - range_in_size_end}, bucket_index - 1);
    }
    return;
  }
  FreeBlocksRangeInfo range_in_size{range_in_size_start, range_in_size_end - range_in_size_start};
  size_t next_size_pow2 =
      (bucket_index + 1 == kSizeBuckets.size()) ? (kSizeBuckets[bucket_index] + 4) : kSizeBuckets[bucket_index + 1];
  uint32_t next_size_blocks_count = 1 << next_size_pow2;
  FreeBlocksTreeBucket bucket{this, bucket_index};
  std::optional<FreeBlocksExtentInfo> join_before;
  FreeBlocksTreeBucket::iterator join_before_iter;
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  auto pos = bucket.find(range_in_size.block_number, false);
  auto ftree = pos.ftree();
  bool joined = false;
  if (!pos.is_end()) {
    // Join with prev if:
    // 1. It ends exactly at this range start.
    // 2. This range start isn't aligned to one size up bucket block.
    // 3. OR joining those ranges will still be smaller than one size up bucket
    FreeBlocksExtentInfo before_pos = *pos;
    if (before_pos.end_block_number() == range_in_size.block_number &&
        (!is_aligned_pow2(range_in_size.block_number, next_size_pow2) ||
         before_pos.blocks_count + range_in_size.blocks_count < next_size_blocks_count)) {
      join_before = *pos;
      join_before_iter = pos;
      range_in_size.block_number = join_before->block_number;
      range_in_size.blocks_count += join_before->blocks_count;
      joined = true;
    }
    // Join with next if:
    // 1. It start  exactly at this range end.
    // 2. This range end isn't aligned to one size up bucket block.
    // 3. OR joining those ranges will still be smaller than one size up bucket
    if (before_pos.block_number <= range_in_size.block_number)  // We could be at begin() before
      ++pos;
    assert(pos.is_end() || (*pos).block_number() > range_in_size.block_number);
    if (!pos.is_end() && (*pos).block_number() == range_in_size.end_block_number() &&
        (!is_aligned_pow2(range_in_size.end_block_number(), next_size_pow2) ||
         (*pos).blocks_count() + range_in_size.blocks_count < next_size_blocks_count)) {
      FreeBlocksExtentInfo join_after = *pos;
      range_in_size.blocks_count += join_after.blocks_count;
      // We give up on the optimization that is done in the original logic (just update the join after block number to
      // the new one if we were going to add it to the same leaf FTree anyway) because:
      // 1. Inn such case simple removal and addition of one key will be pretty simple anyway.
      // 2. In case of maximum bucket size, the iterator can change so we can't just save it, we will need to keep
      // tracking it, which will make the code more complex because we don't optimize it in such way anyway currently.
      bucket.erase(pos, blocks_to_delete);
      joined = true;
    }
  }
  FreeBlocksRangeInfo sub_range = range_in_size;
  while (sub_range.blocks_count) {
    if (sub_range.blocks_count >= next_size_blocks_count) {
      if (bucket_index == kSizeBuckets.size() - 1) {
        // Maximum bucket, use blocks count until alignment)
        sub_range.blocks_count = align_ceil_pow2(sub_range.block_number + 1, next_size_pow2) - sub_range.block_number;
      } else if (joined) {
        if (join_before)
          bucket.erase(join_before_iter, blocks_to_delete);
        AddFreeBlocksForSize(sub_range, bucket_index + 1);
        break;
      }
    }
    assert(sub_range.blocks_count > 0 && sub_range.blocks_count % size_blocks_count == 0 &&
           sub_range.blocks_count / size_blocks_count <= 0x10);
    auto new_value = static_cast<nibble>(sub_range.blocks_count / size_blocks_count - 1);
    if (join_before && sub_range.block_number == range_in_size.block_number) {
      (*join_before_iter).value = new_value;
    } else {
      // Don't use pos to insert because:
      // 1. Our find may go back so it isn't the exact location to insert it.
      // 2. Won't work when inserting multiple items (in maximum bucket size), we will need to keep updating the
      // iterator and we don't do it currently.
      bucket.insert({sub_range.block_number, new_value});
    }
    sub_range.block_number += sub_range.blocks_count;
    sub_range.blocks_count = range_in_size.end_block_number() - sub_range.block_number;
  }
  if (range_in_size.block_number > range.block_number)
    AddFreeBlocksForSize({range.block_number, range_in_size.block_number - range.block_number}, bucket_index - 1);
  if (range_in_size.end_block_number() < range.end_block_number())
    AddFreeBlocksForSize(
        {range_in_size.end_block_number(), range.end_block_number() - range_in_size.end_block_number()},
        bucket_index - 1);
  // Another thing that we do differently, clean up the tree properly if we somehow got empty FTrees after this
  // operation. (In the original code it may leave the tree with empty FTrees..)
  std::ranges::for_each(blocks_to_delete, std::bind(&FreeBlocksAllocator::AddFreeBlocks, this, std::placeholders::_1));
  if (!blocks_to_delete.empty())
    RecreateEPTreeIfNeeded();
}

bool FreeBlocksAllocator::RemoveFreeBlocksExtent(FreeBlocksExtentInfo extent) {
  if (RemoveSpecificFreeBlocksExtent(extent))
    return true;
  // Fallback to slow removal
  for (auto block_number = extent.block_number; block_number < extent.end_block_number(); ++block_number) {
    if (!RemoveSpecificFreeBlocksExtent({block_number, 1, 0}))
      return false;
  }
  return true;
}

bool FreeBlocksAllocator::RemoveSpecificFreeBlocksExtent(FreeBlocksExtentInfo extent) {
  // Iterator over higher bucket sizes in case it merged into one.
  for (auto bucket_index = extent.bucket_index; bucket_index < kSizeBuckets.size(); ++bucket_index) {
    FreeBlocksTreeBucket bucket{this, bucket_index};
    auto res = bucket.find(extent.block_number, false);
    if (res.is_end())
      continue;
    FreeBlocksExtentInfo full_extent = *res;
    if (full_extent.end_block_number() < extent.end_block_number())
      continue;  // out of extent
    std::vector<FreeBlocksRangeInfo> blocks_to_delete;
    if (extent.block_number)
      bucket.erase(res, blocks_to_delete);
    std::ranges::for_each(blocks_to_delete,
                          std::bind(&FreeBlocksAllocator::AddFreeBlocks, this, std::placeholders::_1));
    if (!blocks_to_delete.empty()) {
      // We deleted some FTrees, check if the tree is trivial and need to be recreated.
      RecreateEPTreeIfNeeded();
    }
    if (extent.block_number > full_extent.block_number)
      AddFreeBlocks({full_extent.block_number, extent.block_number - full_extent.block_number});
    if (extent.end_block_number() < full_extent.end_block_number())
      AddFreeBlocks({extent.end_block_number(), full_extent.end_block_number() - extent.end_block_number()});
    mutable_header()->free_blocks_count -= full_extent.blocks_count;
    return true;
  }
  return false;
}

void FreeBlocksAllocator::RecreateEPTreeIfNeeded() {
  // Check if the EPTree is trivial and can be recreated.
  EPTree eptree{this};
  if (eptree.tree_header()->depth.value() <= 1 && eptree.tree_header()->current_tree.tree_depth.value() == 0) {
    // Already trivial
    return;
  }
  auto last = eptree.end()--;
  if ((*last).key || (*last).value != 2)
    return;
  uint32_t last_value = (*last).value;
  assert(last_value == 2);
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  // eptree is empty (aka have only initial FTreee), resize it to one eptree
  auto nodes = last.nodes();
  for (auto& [node_level, node_it] : std::views::reverse(nodes)) {
    if (node_level->header() == &eptree.tree_header()->current_tree) {
      // this is the root, reinitialize it
      node_level->Init(1, node_level->tree_header()->block_number.value());
      node_level->insert({0, last_value});
    } else {
      blocks_to_delete.push_back({node_level->tree_header()->block_number.value(), 1});
    }
  }
  std::ranges::for_each(blocks_to_delete, std::bind(&FreeBlocksAllocator::AddFreeBlocks, this, std::placeholders::_1));
}

bool FreeBlocksAllocator::IsRangeIsFree(FreeBlocksRangeInfo range) {
  FreeBlocksTree tree{this};
  auto pos = tree.find(range.block_number, /*exact_match=*/false);
  if (pos.is_end())
    return false;
  // Check intersection with the free range before us.
  if (range.block_number >= (*pos).block_number() && range.block_number < (*pos).end_block_number())
    return true;
  ++pos;
  if (pos.is_end())
    return false;
  // Check intersection with the free range after us.
  return (*pos).block_number() >= range.block_number && (*pos).block_number() < range.end_block_number();
}

std::optional<std::vector<uint32_t>> FreeBlocksAllocator::AllocBlocks(uint32_t chunks_count,
                                                                      Block::BlockSizeType size,
                                                                      bool use_cache) {
  std::vector<uint32_t> result;
  result.reserve(chunks_count);
  size_t size_index = BlockSizeToIndex(size);
  uint32_t need_more_blocks_count = chunks_count << size;
  if (!need_more_blocks_count)
    return result;
  if (use_cache && size_index == 0 && BlocksCacheSizeLog2()) {
    do {
      auto blocks_from_cache = std::min(need_more_blocks_count, header()->free_blocks_cache_count.value());
      if (!blocks_from_cache)
        continue;
      auto* header = mutable_header();
      auto cache_block_number = header->free_blocks_cache.value();
      header->free_blocks_cache += blocks_from_cache;
      header->free_blocks_cache_count -= blocks_from_cache;
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
  if (AllocBlocksOfSpecificSize(need_more_blocks_count, size_index, kSizeBuckets.size(), result))
    return result;
  // Not enough free blocks
  // TODO: Free cache an try again
  return std::nullopt;
}

bool FreeBlocksAllocator::ReplanishBlocksCache() {
  assert(header()->free_blocks_cache_count.value() == 0);
  uint32_t blocks_to_alloc = 1 << BlocksCacheSizeLog2();
  std::optional<FreeBlocksExtentInfo> selected_extent;
  for (size_t i = 0; i < kSizeBuckets.size(); ++i) {
    FreeBlocksTreeBucket bucket(this, i);
    auto it = bucket.begin();
    if (it.is_end())
      continue;
    FreeBlocksExtentInfo extent = *it;
    if (extent.blocks_count >= blocks_to_alloc * 2 &&
        (!selected_extent || extent.block_number < selected_extent->block_number))
      selected_extent = extent;
  }
  if (!selected_extent)
    return false;
  // Round the block number to be aligned to blocks cache in the whole disk
  auto abs_block_number = area_->to_device_block_number(selected_extent->block_number);
  auto aligned_block_number =
      align_ceil_pow2(abs_block_number, BlocksCacheSizeLog2() + area_->block_size_log2() - Block::BlockSize::Basic);
  selected_extent->block_number = area_->to_area_block_number(aligned_block_number);
  selected_extent->blocks_count = blocks_to_alloc;
  auto* header = mutable_header();
  header->free_blocks_cache = selected_extent->block_number;
  header->free_blocks_cache_count = selected_extent->blocks_count;
  header->free_blocks_count += selected_extent->blocks_count;
  RemoveFreeBlocksExtent(*selected_extent);
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
    if (!blocks_count)
      break;
  }
  if (blocks_count) {
    // Not enough free blocks under those conditions
    return false;
  }

  for (const auto& extent : extents) {
    for (uint32_t i = 0; i < extent.blocks_count >> kSizeBuckets[size_index]; ++i)
      result.push_back(extent.block_number + (i << kSizeBuckets[size_index]));
  }
  std::ranges::for_each(extents, std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
  return true;
}

std::optional<std::vector<FreeBlocksRangeInfo>> FreeBlocksAllocator::AllocAreaBlocks(uint32_t chunks_count,
                                                                                     Block::BlockSizeType size) {
  struct range_info {
    FreeBlocksRangeInfo range;
    std::vector<FreeBlocksExtentInfo> extents;
  };
  size_t size_index = BlockSizeToIndex(size);
  uint32_t wanted_blocks_count = chunks_count << size;
  FreeBlocksTree tree{this};
  std::vector<range_info> ranges;
  std::optional<range_info> selected_range;
  for (const auto& extent : std::views::reverse(tree)) {
    if (extent.bucket_index < size_index)
      continue;
    if (!ranges.empty() && ranges.back().range.block_number == extent.end_block_number()) {
      ranges.back().range.blocks_count += extent.blocks_count();
      ranges.back().range.block_number = extent.block_number();
      ranges.back().extents.push_back(extent);
    } else {
      ranges.push_back({{extent.block_number(), extent.blocks_count()}, {extent}});
    }
    if (ranges.back().range.blocks_count >= wanted_blocks_count) {
      selected_range = ranges.back();
      break;
    }
  }
  if (selected_range) {
    // remove  unneeded blocks
    auto remainder = selected_range->range.blocks_count - wanted_blocks_count;
    selected_range->extents.back().block_number += remainder;
    selected_range->extents.back().blocks_count -= remainder;
    selected_range->range.block_number += remainder;
    selected_range->range.blocks_count = wanted_blocks_count;
    std::ranges::for_each(selected_range->extents,
                          std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
    return std::vector<FreeBlocksRangeInfo>{selected_range->range};
  }
  // use the bigger chunkis first
  std::ranges::sort(ranges, [](const auto& a, const auto& b) { return b.range.blocks_count < a.range.blocks_count; });
  uint32_t total_blocks{0};
  std::vector<range_info> used_ranges;
  for (auto& range : ranges) {
    total_blocks += range.range.blocks_count;
    used_ranges.push_back(range);
    if (total_blocks >= wanted_blocks_count)
      break;
  }
  if (total_blocks < wanted_blocks_count || used_ranges.size() > 0x100)
    return std::nullopt;  // not enough space
  // now sort by block number
  std::ranges::sort(used_ranges,
                    [](const auto& a, const auto& b) { return a.range.block_number < b.range.blocks_count; });
  // remove from first chunk unneeded blocks
  auto remainder = total_blocks - wanted_blocks_count;
  used_ranges[0].range.block_number += remainder;
  used_ranges[0].range.blocks_count -= remainder;
  while (used_ranges[0].extents.back().end_block_number() <= used_ranges[0].range.block_number) {
    remainder -= used_ranges[0].extents.back().blocks_count;
    used_ranges[0].extents.pop_back();
  }
  used_ranges[0].extents.back().block_number += remainder;
  used_ranges[0].extents.back().blocks_count -= remainder;
  for (const auto& range : used_ranges)
    std::ranges::for_each(range.extents,
                          std::bind(&FreeBlocksAllocator::RemoveFreeBlocksExtent, this, std::placeholders::_1));
  return used_ranges | std::views::transform([](const auto& x) { return x.range; }) | std::ranges::to<std::vector>();
}

std::shared_ptr<Block> FreeBlocksAllocator::LoadAllocatorBlock(uint32_t block_number, bool new_block) {
  return throw_if_error(area_->LoadMetadataBlock(block_number, new_block));
}

const FreeBlocksAllocatorHeader* FreeBlocksAllocator::header() const {
  return block_->get_object<FreeBlocksAllocatorHeader>(sizeof(MetadataBlockHeader));
}

FreeBlocksAllocatorHeader* FreeBlocksAllocator::mutable_header() {
  return block_->get_mutable_object<FreeBlocksAllocatorHeader>(sizeof(MetadataBlockHeader));
}

size_t FreeBlocksAllocator::BlocksCacheSizeLog2() const {
  if (area_->blocks_count() >> (24 - Block::BlockSize::Basic)) {
    if (area_->blocks_count() >> (30 - area_->block_size_log2())) {
      return 23 - area_->block_size_log2();
    } else {
      return 21 - area_->block_size_log2();
    }
  } else {
    return 0;
  }
}
