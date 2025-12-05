/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

#include "directory_tree.h"

#include "utils/test_block.h"
#include "utils/test_blocks_device.h"
#include "utils/test_free_blocks_allocator.h"
#include "utils/test_utils.h"

namespace {

template <typename Tuple, std::size_t... I>
std::string tuple_to_string_impl(const Tuple& tpl, std::index_sequence<I...>) {
  return std::string{std::get<I>(tpl)...};
}
template <typename... T>
std::string tuple_to_string(const std::tuple<T...>& tpl) {
  return tuple_to_string_impl(tpl, std::index_sequence_for<T...>{});
}

class TestDirectoryTree : public DirectoryTree<uint16_t> {
 public:
  TestDirectoryTree() = default;
  TestDirectoryTree(std::shared_ptr<Block> block) : DirectoryTree<uint16_t>(std::move(block)) {}

  int allocated_bytes() {
    int allocated = 1 << log2_size(BlockSize::Logical);
    for (int i = 3; i <= 10; ++i) {
      allocated -= header()->free_list[i - 3].free_blocks_count.value() << i;
    }
    return allocated - 64;
  }

  int free_bytes() {
    int free_bytes = 0;
    for (int i = 3; i <= 10; ++i) {
      free_bytes += header()->free_list[i - 3].free_blocks_count.value() << i;
    }
    return free_bytes;
  }

  void copy_value(DirectoryTree&, parent_node&, dir_leaf_tree_value_type) const override {}

  std::shared_ptr<DirectoryTree<uint16_t>> create(std::shared_ptr<Block> block) const override {
    return std::make_shared<TestDirectoryTree>(std::move(block));
  }
};

}  // namespace

TEST_CASE("DirectoryTreeTests") {
  auto test_device = std::make_shared<TestBlocksDevice>();
  auto directory_tree_block = TestBlock::LoadMetadataBlock(test_device, 0);
  TestDirectoryTree dir_tree{directory_tree_block};
  dir_tree.Init(true);

  SECTION("Check empty tree size") {
    REQUIRE(dir_tree.size() == 0);
  }

  SECTION("insert linear entries sorted") {
    const int kEntries = 10;
    for (uint16_t i = 0; i < kEntries; ++i) {
      REQUIRE(dir_tree.insert({std::string(i + 1, 'a'), i}));
    }
    REQUIRE(dir_tree.size() == kEntries);
    for (auto [i, entry] : std::views::enumerate(dir_tree)) {
      CHECK(entry.key() == std::string(i + 1, 'a'));
      CHECK(entry.value() == static_cast<uint16_t>(i));
    }
  }

  SECTION("insert linear entries reverse") {
    const int kEntries = 10;
    for (int i = 0; i < kEntries; ++i) {
      REQUIRE(dir_tree.insert({std::string(kEntries - i, 'a'), static_cast<uint16_t>(kEntries - 1 - i)}));
    }
    REQUIRE(dir_tree.size() == kEntries);
    for (auto [i, entry] : std::views::enumerate(dir_tree)) {
      CHECK(entry.key() == std::string(i + 1, 'a'));
      CHECK(entry.value() == static_cast<uint16_t>(i));
    }
  }

  SECTION("insert and erase entries randomlly") {
    constexpr std::array<char, 3> chars = {'a', 'b', 'c'};
    auto one_char =
        std::ranges::to<std::vector>(std::views::cartesian_product(chars) |
                                     std::views::transform([](auto tuple) { return tuple_to_string(tuple); }));
    auto two_chars =
        std::ranges::to<std::vector>(std::views::cartesian_product(chars, chars) |
                                     std::views::transform([](auto tuple) { return tuple_to_string(tuple); }));
    auto three_chars =
        std::ranges::to<std::vector>(std::views::cartesian_product(chars, chars, chars) |
                                     std::views::transform([](auto tuple) { return tuple_to_string(tuple); }));
    auto four_chars =
        std::ranges::to<std::vector>(std::views::cartesian_product(chars, chars, chars, chars) |
                                     std::views::transform([](auto tuple) { return tuple_to_string(tuple); }));
    auto keys =
        std::ranges::to<std::vector>(std::views::join(std::vector{one_char, two_chars, three_chars, four_chars}));
    std::ranges::sort(keys);

    auto unsorted_keys_indxes = createShuffledKeysArray<3 + 3 * 3 + 3 * 3 * 3 + 3 * 3 * 3 * 3>();
    for (uint16_t i = 0; i < keys.size(); ++i) {
      REQUIRE(dir_tree.insert({keys[unsorted_keys_indxes[i]], static_cast<uint16_t>(unsorted_keys_indxes[i])}));
    }
    REQUIRE(dir_tree.size() == keys.size());
    REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == static_cast<int>(keys.size()));
    for (auto [i, entry] : std::views::enumerate(dir_tree)) {
      CHECK(entry.key() == keys[i]);
      CHECK(entry.value() == static_cast<uint16_t>(i));
    }
    for (uint16_t i = 0; i < keys.size(); ++i) {
      auto it = dir_tree.find(keys[unsorted_keys_indxes[i]]);
      REQUIRE(it != dir_tree.end());
      dir_tree.erase(it);
    }
    REQUIRE(dir_tree.size() == 0);
    REQUIRE(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == 0);

    REQUIRE(dir_tree.allocated_bytes() == 0);
  }

  SECTION("insert alphabet") {
    for (char i = 'a'; i <= 'z'; ++i) {
      REQUIRE(dir_tree.insert({std::string{i}, static_cast<uint16_t>(i)}));
    }
    REQUIRE(dir_tree.size() == 26);
    char i = 'a';
    for (const auto& entry : dir_tree) {
      CHECK(entry.key() == std::string{i});
      CHECK(entry.value() == static_cast<uint16_t>(i++));
    }
  }

  SECTION("fail to alloc new node") {
    for (int i = 0; i < 7; ++i)
      dir_tree.Alloc(0x400);
    dir_tree.Alloc(0x200);
    dir_tree.Alloc(0x100);
    dir_tree.Alloc(0x80);
    dir_tree.Alloc(0x20);
    dir_tree.Alloc(0x10);
    dir_tree.Alloc(0x8);
    REQUIRE(dir_tree.free_bytes() == 8);
    REQUIRE(dir_tree.insert({"a", 0}));
    CHECK(dir_tree.size() == 1);
    CHECK((*dir_tree.begin()).key() == "a");
    CHECK(!dir_tree.insert({"b", 0}));
    CHECK(dir_tree.size() == 1);
    CHECK((*dir_tree.begin()).key() == "a");
  }

  SECTION("fail to split") {
    for (int i = 0; i < 7; ++i)
      dir_tree.Alloc(0x400);
    dir_tree.Alloc(0x200);
    dir_tree.Alloc(0x100);
    dir_tree.Alloc(0x80);
    dir_tree.Alloc(0x20);
    dir_tree.Alloc(0x10);
    REQUIRE(dir_tree.free_bytes() == 0x10);
    REQUIRE(dir_tree.insert({"a", 0}));
    CHECK(dir_tree.size() == 1);
    CHECK((*dir_tree.begin()).key() == "a");
    CHECK(dir_tree.free_bytes() == 8);
    CHECK(!dir_tree.insert({"b", 0}));
    CHECK(dir_tree.size() == 1);
    CHECK((*dir_tree.begin()).key() == "a");
    CHECK(dir_tree.free_bytes() == 8);
  }

  SECTION("shrink when split and full") {
    for (int i = 0; i < 7; ++i)
      dir_tree.Alloc(0x400);
    dir_tree.Alloc(0x200);
    dir_tree.Alloc(0x100);
    dir_tree.Alloc(0x80);
    REQUIRE(dir_tree.free_bytes() == 0x40);
    REQUIRE(dir_tree.insert({"aaaaaaaaaaaaaaaaaaaaaaa", 0}));
    CHECK(dir_tree.free_bytes() == 0x20);
    CHECK(dir_tree.size() == 1);
    CHECK((*dir_tree.begin()).key() == "aaaaaaaaaaaaaaaaaaaaaaa");
    // This will cause a shrink
    REQUIRE(dir_tree.insert({"a", 0}));
    CHECK(dir_tree.free_bytes() == 0x10);
    CHECK(dir_tree.size() == 2);
    auto it = dir_tree.begin();
    CHECK((*it).key() == "a");
    CHECK((*++it).key() == "aaaaaaaaaaaaaaaaaaaaaaa");
    // This will cause a second shrink
    REQUIRE(dir_tree.insert({"b", 0}));
    CHECK(dir_tree.free_bytes() == 8);
    CHECK(dir_tree.size() == 3);
    it = dir_tree.begin();
    CHECK((*it).key() == "a");
    CHECK((*++it).key() == "aaaaaaaaaaaaaaaaaaaaaaa");
    CHECK((*++it).key() == "b");
  }

  SECTION("merge when full") {
    for (int i = 0; i < 7; ++i)
      dir_tree.Alloc(0x400);
    dir_tree.Alloc(0x200);
    dir_tree.Alloc(0x100);
    dir_tree.Alloc(0x80);
    dir_tree.Alloc(0x10);
    REQUIRE(dir_tree.free_bytes() == 0x30);
    REQUIRE(dir_tree.insert({"aaaaaaaaaaaa", 1}));
    REQUIRE(dir_tree.insert({"aaaaaaaab", 2}));
    REQUIRE(dir_tree.free_bytes() == 0x10);
    auto it = dir_tree.find("aaaaaaaab");
    REQUIRE(it != dir_tree.end());
    dir_tree.erase(it);
    CHECK(dir_tree.size() == 1);
    CHECK(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == 1);
    CHECK((*dir_tree.begin()).key() == "aaaaaaaaaaaa");
    CHECK((*dir_tree.begin()).value() == 1);
    CHECK(dir_tree.allocated_bytes() == 0x20);
  }

  SECTION("merge when full 2") {
    for (int i = 0; i < 7; ++i)
      dir_tree.Alloc(0x400);
    dir_tree.Alloc(0x200);
    dir_tree.Alloc(0x100);
    dir_tree.Alloc(0x40);
    dir_tree.Alloc(0x20);
    dir_tree.Alloc(0x8);
    REQUIRE(dir_tree.insert({"b", 1}));
    REQUIRE(dir_tree.insert({"aaaaaaaaaaaa", 2}));
    REQUIRE(dir_tree.insert({"aaaaaaaab", 3}));
    REQUIRE(dir_tree.insert({"aaaaaaaaaaaab", 4}));
    REQUIRE(dir_tree.insert({"aaaaaaaaaaaac", 5}));
    CHECK(dir_tree.free_bytes() == 0x10);
    auto it = dir_tree.find("aaaaaaaab");
    REQUIRE(it != dir_tree.end());
    dir_tree.erase(it);
    CHECK(dir_tree.size() == 4);
    CHECK(std::ranges::distance(dir_tree.begin(), dir_tree.end()) == 4);
    it = dir_tree.begin();
    CHECK((*it).key() == "aaaaaaaaaaaa");
    CHECK((*it++).value() == 2);
    CHECK((*it).key() == "aaaaaaaaaaaab");
    CHECK((*it++).value() == 4);
    CHECK((*it).key() == "aaaaaaaaaaaac");
    CHECK((*it++).value() == 5);
    CHECK((*it).key() == "b");
    CHECK((*it++).value() == 1);
    CHECK(dir_tree.allocated_bytes() == 0x40);
  }

  SECTION("split") {
    REQUIRE(dir_tree.insert({"a", 1}));
    REQUIRE(dir_tree.insert({"a1", 2}));
    REQUIRE(dir_tree.insert({"aa", 3}));
    REQUIRE(dir_tree.insert({"aaaaa1", 4}));
    REQUIRE(dir_tree.insert({"aaaaa2", 5}));
    REQUIRE(dir_tree.insert({"aaaaa2a", 6}));
    REQUIRE(dir_tree.insert({"aaaab1", 7}));
    REQUIRE(dir_tree.insert({"aaaab2", 8}));
    auto split_point = dir_tree.find("aaaaa2a");
    REQUIRE(split_point != dir_tree.end());
    REQUIRE(dir_tree.size() == 8);

    std::array<TestDirectoryTree, 2> new_trees{TestDirectoryTree{TestBlock::LoadMetadataBlock(test_device, 1)},
                                               TestDirectoryTree{TestBlock::LoadMetadataBlock(test_device, 2)}};
    new_trees[0].Init(false);
    new_trees[1].Init(false);

    dir_tree.split(new_trees[0], new_trees[1], split_point);
    CHECK(new_trees[0].size() == 5);
    CHECK(new_trees[1].size() == 3);
    CHECK(std::ranges::distance(new_trees[0].begin(), new_trees[0].end()) == 5);
    CHECK(std::ranges::distance(new_trees[1].begin(), new_trees[1].end()) == 3);
    auto it = new_trees[0].begin();
    CHECK((*it).key() == "a");
    CHECK((*it++).value() == 1);
    CHECK((*it).key() == "a1");
    CHECK((*it++).value() == 2);
    CHECK((*it).key() == "aa");
    CHECK((*it++).value() == 3);
    CHECK((*it).key() == "aaaaa1");
    CHECK((*it++).value() == 4);
    CHECK((*it).key() == "aaaaa2");
    CHECK((*it++).value() == 5);
    it = new_trees[1].begin();
    CHECK((*it).key() == "aaaaa2a");
    CHECK((*it++).value() == 6);
    CHECK((*it).key() == "aaaab1");
    CHECK((*it++).value() == 7);
    CHECK((*it).key() == "aaaab2");
    CHECK((*it++).value() == 8);
  }

  SECTION("find non exact") {
    // Should cover all the possible cases
    static const std::vector<std::string> kKeys{
        {"2", "3", "33", "33331", "333311", "333312", "33333", "5", "55", "5555", "555555"}};
    static const std::vector<std::tuple<std::string, std::string>> kExpectedResults{{{"0", "2"},
                                                                                     {"3", "3"},
                                                                                     {"32", "3"},
                                                                                     {"330", "33"},
                                                                                     {"3331", "33"},
                                                                                     {"33332", "333312"},
                                                                                     {"3334", "33333"},
                                                                                     {"4", "33333"},
                                                                                     {"54", "5"},
                                                                                     {"5554", "55"},
                                                                                     {"55554", "5555"},
                                                                                     {"555556", "555555"},
                                                                                     {"6", "555555"}}};
    for (const auto& key : kKeys)
      REQUIRE(dir_tree.insert({key, 0}));
    for (const auto& [key, result] : kExpectedResults) {
      auto it = dir_tree.find(key, /*exact_match=*/false);
      REQUIRE(it != dir_tree.end());
      CAPTURE(key);
      CHECK((*it).key() == result);
    }
  }
}
