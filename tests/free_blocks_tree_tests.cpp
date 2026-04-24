/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "free_blocks_tree.h"
#include "free_blocks_tree_bucket.h"

#include "utils/range_assertions.h"
#include "utils/test_fixtures.h"
#include "utils/test_utils.h"

namespace {

class FreeBlocksTreeFixture : public FreeBlocksAllocatorFixture {
 public:
  FreeBlocksTreeFixture() { allocator_initialized = allocator.Init(1000000); }

  bool allocator_initialized = false;
  FreeBlocksTree tree{&allocator};
};

constexpr int kFreeBlocksTreeStressItems = 600 * 300;

void InsertIntoBucket(TestFreeBlocksAllocator& allocator, uint32_t key) {
  REQUIRE(FreeBlocksTreeBucket{&allocator, key % kSizeBuckets.size()}.insert({key, static_cast<nibble>(key % 16)}));
}

std::vector<std::tuple<uint32_t, nibble, size_t>> ExpectedTreeExtents(uint32_t count) {
  std::vector<std::tuple<uint32_t, nibble, size_t>> values;
  values.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    values.emplace_back(i, static_cast<nibble>(i % 16), static_cast<size_t>(i % kSizeBuckets.size()));
  }
  return values;
}

}  // namespace

TEST_CASE_METHOD(FreeBlocksTreeFixture, "FreeBlocksTree is empty after initialization", "[free-blocks][tree][unit]") {
  REQUIRE(allocator_initialized);

  REQUIRE(std::distance(tree.begin(), tree.end()) == 0);
}

TEST_CASE_METHOD(FreeBlocksTreeFixture, "FreeBlocksTree inserts sorted extents", "[free-blocks][tree][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kFreeBlocksTreeStressItems; ++i) {
    InsertIntoBucket(allocator, i);
  }

  REQUIRE(CollectFreeExtents(tree) == ExpectedTreeExtents(kFreeBlocksTreeStressItems));

  for (uint32_t i = 0; i < kFreeBlocksTreeStressItems; ++i) {
    CAPTURE(i);
    REQUIRE((*tree.find(i)).key() == i);
  }
}

TEST_CASE_METHOD(FreeBlocksTreeFixture,
                 "FreeBlocksTree keeps unsorted inserts ordered and searchable",
                 "[free-blocks][tree][stress]") {
  REQUIRE(allocator_initialized);

  auto unsorted_keys = createShuffledKeysArray<kFreeBlocksTreeStressItems>();
  for (auto key : unsorted_keys) {
    InsertIntoBucket(allocator, key);
  }

  auto sorted_keys = unsorted_keys;
  std::ranges::sort(sorted_keys);
  REQUIRE(CollectKeys(tree) == sorted_keys);
  REQUIRE(CollectFreeExtents(tree) == ExpectedTreeExtents(kFreeBlocksTreeStressItems));

  for (uint32_t i = 0; i < kFreeBlocksTreeStressItems; ++i) {
    CAPTURE(i);
    REQUIRE((*tree.find(i)).key() == i);
  }
}

TEST_CASE_METHOD(FreeBlocksTreeFixture,
                 "FreeBlocksTree erases shuffled extents and releases backing blocks",
                 "[free-blocks][tree][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kFreeBlocksTreeStressItems; ++i) {
    InsertIntoBucket(allocator, i);
  }

  auto unsorted_keys = createShuffledKeysArray<kFreeBlocksTreeStressItems>();
  auto middle = unsorted_keys.begin() + kFreeBlocksTreeStressItems / 2;
  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  for (auto key : std::ranges::subrange(unsorted_keys.begin(), middle)) {
    CAPTURE(key);
    REQUIRE(FreeBlocksTreeBucket{&allocator, key % kSizeBuckets.size()}.erase(key, blocks_to_delete));
  }

  auto sorted_upper_half = std::ranges::to<std::vector>(std::ranges::subrange(middle, unsorted_keys.end()));
  std::ranges::sort(sorted_upper_half);
  REQUIRE(CollectKeys(tree) == sorted_upper_half);

  for (auto key : std::ranges::subrange(middle, unsorted_keys.end())) {
    CAPTURE(key);
    REQUIRE(FreeBlocksTreeBucket{&allocator, key % kSizeBuckets.size()}.erase(key, blocks_to_delete));
  }

  REQUIRE(tree.begin() == tree.end());

  auto blocks_numbers_to_delete = CollectRange(blocks_to_delete, [](const FreeBlocksRangeInfo& range) {
    return range.block_number;
  });
  std::ranges::sort(blocks_numbers_to_delete);
  REQUIRE(blocks_numbers_to_delete ==
          SequentialKeys(allocator.AllocFreeBlockFromCache() - allocator.initial_frees_block_number(),
                         allocator.initial_frees_block_number()));
}

TEST_CASE_METHOD(FreeBlocksTreeFixture,
                 "FreeBlocksTree skips empty FTrees during non-exact find",
                 "[free-blocks][tree][unit]") {
  REQUIRE(allocator_initialized);

  EPTree eptree{&allocator};
  FreeBlocksTreeBucket bucket{&allocator, 3};
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
  REQUIRE(tree.find(150, false) == tree.end());

  REQUIRE(bucket.insert({50, nibble{0}}));
  REQUIRE(ftree1.size() == 1);
  REQUIRE(ftree2.empty());
  REQUIRE(ftree3.empty());
  REQUIRE(tree.find(150, false) != tree.end());
  REQUIRE((*tree.find(150, false)).key() == 50);
  REQUIRE((*tree.find(25, false)).key() == 50);
  REQUIRE(CollectKeys(tree) == std::vector<uint32_t>{50});
  REQUIRE(CollectKeys(std::views::reverse(tree)) == std::vector<uint32_t>{50});

  REQUIRE(bucket.insert({160, nibble{0}}));
  REQUIRE(ftree1.size() == 1);
  REQUIRE(ftree2.size() == 1);
  REQUIRE(ftree3.empty());
  REQUIRE((*tree.find(150, false)).key() == 50);
  REQUIRE((*tree.find(250, false)).key() == 160);
  REQUIRE(CollectKeys(tree) == std::vector<uint32_t>{50, 160});
  REQUIRE(CollectKeys(std::views::reverse(tree)) == std::vector<uint32_t>{160, 50});

  std::vector<FreeBlocksRangeInfo> blocks_to_delete;
  REQUIRE(bucket.erase(50, blocks_to_delete));
  REQUIRE(blocks_to_delete.empty());
  REQUIRE(ftree1.empty());
  REQUIRE(ftree2.size() == 1);
  REQUIRE(ftree3.empty());
  REQUIRE((*tree.find(150, false)).key() == 160);
  REQUIRE((*tree.find(250, false)).key() == 160);
  REQUIRE((*tree.find(25, false)).key() == 160);
  REQUIRE(CollectKeys(tree) == std::vector<uint32_t>{160});
  REQUIRE(CollectKeys(std::views::reverse(tree)) == std::vector<uint32_t>{160});
}

TEST_CASE_METHOD(FreeBlocksTreeFixture,
                 "FreeBlocksTree iterator walks forward and backward",
                 "[free-blocks][tree][iterator][stress]") {
  REQUIRE(allocator_initialized);

  for (uint32_t i = 0; i < kFreeBlocksTreeStressItems; ++i) {
    InsertIntoBucket(allocator, i);
  }

  RequireBidirectionalIteration(tree, kFreeBlocksTreeStressItems, [](const auto& extent) { return extent.key(); });
}
