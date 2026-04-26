/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "free_blocks_tree_bucket.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"
#include "utils/test_utils.h"

namespace {

class FreeBlocksTreeBucketFixture : public FreeBlocksAllocatorFixture {
 public:
  FreeBlocksTreeBucketFixture() { allocator_initialized = allocator.Init(1000000); }

  bool allocator_initialized = false;
  FreeBlocksTreeBucket bucket{&allocator, 3};
};

constexpr int kFreeBlocksBucketStressItems = 600 * 300;

}  // namespace

TEST_CASE_METHOD(FreeBlocksTreeBucketFixture,
                 "FreeBlocksTreeBucket is empty after initialization",
                 "[free-blocks][tree-bucket][unit]") {
  REQUIRE(allocator_initialized);

  REQUIRE(std::distance(bucket.begin(), bucket.end()) == 0);
}

TEST_CASE_METHOD(FreeBlocksTreeBucketFixture,
                 "FreeBlocksTreeBucket inserts sorted extents",
                 "[free-blocks][tree-bucket][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kFreeBlocksBucketStressItems; ++i) {
    REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
  }

  REQUIRE(CollectFreeExtents(bucket) == SequentialFreeExtents(kFreeBlocksBucketStressItems, 3));

  for (uint32_t i = 0; i < kFreeBlocksBucketStressItems; ++i) {
    CAPTURE(i);
    REQUIRE((*bucket.find(i, true)).key() == i);
  }
}

TEST_CASE_METHOD(FreeBlocksTreeBucketFixture,
                 "FreeBlocksTreeBucket keeps unsorted inserts ordered and searchable",
                 "[free-blocks][tree-bucket][stress]") {
  REQUIRE(allocator_initialized);

  auto unsorted_keys = createShuffledKeysArray<kFreeBlocksBucketStressItems>();
  for (auto key : unsorted_keys) {
    REQUIRE(bucket.insert({key, static_cast<nibble>(key % 16)}));
  }

  auto sorted_keys = unsorted_keys;
  std::ranges::sort(sorted_keys);
  REQUIRE(CollectKeys(bucket) == sorted_keys);
  REQUIRE(CollectFreeExtents(bucket) == SequentialFreeExtents(kFreeBlocksBucketStressItems, 3));

  for (uint32_t i = 0; i < kFreeBlocksBucketStressItems; ++i) {
    CAPTURE(i);
    REQUIRE((*bucket.find(i, true)).key() == i);
  }
}

TEST_CASE_METHOD(FreeBlocksTreeBucketFixture,
                 "FreeBlocksTreeBucket erases shuffled extents and releases backing blocks",
                 "[free-blocks][tree-bucket][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kFreeBlocksBucketStressItems; ++i) {
    REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
  }

  auto unsorted_keys = createShuffledKeysArray<kFreeBlocksBucketStressItems>();
  auto middle = unsorted_keys.begin() + kFreeBlocksBucketStressItems / 2;
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
    CAPTURE(key);
    REQUIRE(bucket.erase(key, blocks_to_delete));
  }

  auto sorted_upper_half = std::ranges::to<std::vector>(std::ranges::subrange(middle, unsorted_keys.end()));
  std::ranges::sort(sorted_upper_half);
  REQUIRE(CollectKeys(bucket) == sorted_upper_half);

  for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
    CAPTURE(key);
    REQUIRE(bucket.erase(key, blocks_to_delete));
  }

  REQUIRE(bucket.begin() == bucket.end());

  auto blocks_numbers_to_delete =
      CollectRange(blocks_to_delete, [](const FreeBlocksRangeInfo& range) { return range.block_number; });
  std::ranges::sort(blocks_numbers_to_delete);
  REQUIRE(blocks_numbers_to_delete ==
          SequentialKeys(allocator.AllocFreeBlockFromCache() - allocator.initial_frees_block_number(),
                         allocator.initial_frees_block_number()));
}

TEST_CASE_METHOD(FreeBlocksTreeBucketFixture,
                 "FreeBlocksTreeBucket skips empty FTrees during non-exact find",
                 "[free-blocks][tree-bucket][unit]") {
  REQUIRE(allocator_initialized);

  EPTree eptree{&allocator};
  auto ftree_100_to_200_block_number = allocator.AllocFreeBlockFromCache();
  auto ftree_100_to_200_block = allocator.LoadAllocatorBlock(ftree_100_to_200_block_number, true);
  FTreesBlock{ftree_100_to_200_block}.Init();
  REQUIRE(eptree.insert({100, ftree_100_to_200_block_number}));
  auto ftree_200_plus_block_number = allocator.AllocFreeBlockFromCache();
  auto ftree_200_plus_block = allocator.LoadAllocatorBlock(ftree_200_plus_block_number, true);
  FTreesBlock{ftree_200_plus_block}.Init();
  REQUIRE(eptree.insert({200, ftree_200_plus_block_number}));

  auto ftree_0_to_100_block = allocator.LoadAllocatorBlock(allocator.initial_ftrees_block_number(), false);
  FTree ftree1{ftree_0_to_100_block, 3};
  FTree ftree2{ftree_100_to_200_block, 3};
  FTree ftree3{ftree_200_plus_block, 3};

  REQUIRE(ftree1.empty());
  REQUIRE(ftree2.empty());
  REQUIRE(ftree3.empty());
  REQUIRE(bucket.find(150, false) == bucket.end());

  REQUIRE(bucket.insert({50, nibble{0}}));
  REQUIRE(ftree1.size() == 1);
  REQUIRE(ftree2.empty());
  REQUIRE(ftree3.empty());
  REQUIRE(bucket.find(150, false) != bucket.end());
  REQUIRE((*bucket.find(150, false)).key() == 50);
  REQUIRE((*bucket.find(25, false)).key() == 50);
  REQUIRE(CollectKeys(bucket) == std::vector<uint32_t>{50});
  REQUIRE(CollectKeys(std::views::reverse(bucket)) == std::vector<uint32_t>{50});

  REQUIRE(bucket.insert({160, nibble{0}}));
  REQUIRE(ftree1.size() == 1);
  REQUIRE(ftree2.size() == 1);
  REQUIRE(ftree3.empty());
  REQUIRE((*bucket.find(150, false)).key() == 50);
  REQUIRE((*bucket.find(250, false)).key() == 160);
  REQUIRE(CollectKeys(bucket) == std::vector<uint32_t>{50, 160});
  REQUIRE(CollectKeys(std::views::reverse(bucket)) == std::vector<uint32_t>{160, 50});

  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  REQUIRE(bucket.erase(50, blocks_to_delete));
  REQUIRE(blocks_to_delete.empty());
  REQUIRE(ftree1.empty());
  REQUIRE(ftree2.size() == 1);
  REQUIRE(ftree3.empty());
  REQUIRE((*bucket.find(150, false)).key() == 160);
  REQUIRE((*bucket.find(250, false)).key() == 160);
  REQUIRE((*bucket.find(25, false)).key() == 160);
  REQUIRE(CollectKeys(bucket) == std::vector<uint32_t>{160});
  REQUIRE(CollectKeys(std::views::reverse(bucket)) == std::vector<uint32_t>{160});
}

TEST_CASE_METHOD(FreeBlocksTreeBucketFixture,
                 "FreeBlocksTreeBucket iterator walks forward and backward",
                 "[free-blocks][tree-bucket][iterator][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kFreeBlocksBucketStressItems; ++i) {
    REQUIRE(bucket.insert({i, static_cast<nibble>(i % 16)}));
  }

  RequireBidirectionalIteration(bucket, kFreeBlocksBucketStressItems, [](const auto& extent) { return extent.key(); });
}

TEST_CASE_METHOD(FreeBlocksAllocatorFixture,
                 "FreeBlocksTreeBucket splits a full FTrees block without cache",
                 "[free-blocks][tree-bucket][regression]") {
  REQUIRE(allocator.Init(0));

  FreeBlocksTreeBucket split_bucket{&allocator, 0};
  constexpr uint32_t kFirstFreeBlock = 1000;
  constexpr uint32_t kInsertedExtents = 2000;
  allocator.set_free_blocks_count_for_testing(kInsertedExtents);

  for (uint32_t i = 0; i < kInsertedExtents; ++i) {
    CAPTURE(i);
    REQUIRE(split_bucket.insert({kFirstFreeBlock + i, nibble{0}}));
  }

  REQUIRE(split_bucket.find(kFirstFreeBlock, true) == split_bucket.end());
  REQUIRE(split_bucket.find(kFirstFreeBlock + kInsertedExtents - 1, true) != split_bucket.end());
}
