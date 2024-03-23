/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <iterator>
#include <memory>
#include <vector>

#include "free_blocks_allocator.h"
#include "ptree.h"
#include "structs.h"
#include "tree_nodes_allocator.h"

class FreeBlocksAllocator;

template <>
PTreeNode<RTreeLeaf_details>::const_iterator split_point(
    const PTreeNode<RTreeLeaf_details>& node,
    const typename PTreeNode<RTreeLeaf_details>::const_iterator& pos,
    key_type& split_key);

static_assert(sizeof(PTreeNode_details) == sizeof(RTreeLeaf_details));
using EPTreeBlock = TreeNodesAllocator<FreeBlocksAllocatorHeader, EPTreeFooter, sizeof(PTreeNode_details)>;

class RTree : public PTree<PTreeNode_details, RTreeLeaf_details, EPTreeBlock> {
 public:
  RTree(std::shared_ptr<MetadataBlock> block)
      : PTree<PTreeNode_details, RTreeLeaf_details, EPTreeBlock>(std::move(block)) {}

  PTreeHeader* mutable_header() override { return &mutable_tree_header()->current_tree; }
  const PTreeHeader* header() const override { return &tree_header()->current_tree; }

  void Init(uint8_t depth);
};

class EPTreeConstIterator {
 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = int;
  using value_type = RTree::iterator::value_type;

  using const_reference = RTree::iterator::const_reference;
  using const_pointer = RTree::iterator::const_pointer;

  using reference = RTree::const_iterator::reference;
  using pointer = RTree::const_iterator::const_pointer;

  using node_info = node_iterator_info<RTree>;

  EPTreeConstIterator() = default;

  EPTreeConstIterator(FreeBlocksAllocator* allocator, std::vector<node_info> nodes)
      : allocator_(allocator), nodes_(std::move(nodes)) {}

  reference operator*() const { return *nodes_.back().iterator; }
  pointer operator->() const { return nodes_.back().iterator.operator->(); }

  EPTreeConstIterator& operator++();
  EPTreeConstIterator& operator--();
  EPTreeConstIterator operator++(int);
  EPTreeConstIterator operator--(int);

  bool operator==(const EPTreeConstIterator& other) const {
    return nodes_.back().iterator == other.nodes_.back().iterator;
  }

  std::vector<node_info>& nodes() { return nodes_; };
  const std::vector<node_info>& nodes() const { return nodes_; };

  bool is_begin() const {
    return std::ranges::all_of(nodes_, [](const node_info& node) { return node.iterator.is_begin(); });
  }
  bool is_end() const { return nodes_.back().iterator.is_end(); }

 private:
  FreeBlocksAllocator* allocator_;

  std::vector<node_info> nodes_;
};
static_assert(std::bidirectional_iterator<EPTreeConstIterator>);

class EPTreeIterator : public EPTreeConstIterator {
 public:
  using base = EPTreeConstIterator;

  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = base::difference_type;
  using value_type = base::value_type;

  using reference = RTree::iterator::reference;
  using pointer = RTree::iterator::pointer;

  using node_info = base::node_info;

  EPTreeIterator() = default;

  EPTreeIterator(FreeBlocksAllocator* allocator, std::vector<node_info> nodes) : base(allocator, std::move(nodes)) {}

  reference operator*() const { return *nodes().back().iterator; }
  pointer operator->() const { return nodes().back().iterator.operator->(); }

  EPTreeIterator& operator++();
  EPTreeIterator& operator--();
  EPTreeIterator operator++(int);
  EPTreeIterator operator--(int);

  bool operator==(const EPTreeIterator& other) const { return base::operator==(other); }
};
static_assert(std::bidirectional_iterator<EPTreeIterator>);

class EPTree : public EPTreeBlock {
 public:
  using iterator = EPTreeIterator;
  using const_iterator = EPTreeConstIterator;
  using reverse_iterator = TreeReverseIterator<iterator>;
  using const_reverse_iterator = TreeReverseIterator<const_iterator>;

  EPTree(FreeBlocksAllocator* allocator) : EPTreeBlock(allocator->root_block()), allocator_(allocator) {}

  iterator begin() { return begin_impl(); }
  iterator end() { return end_impl(); }
  const_iterator begin() const { return begin_impl(); }
  const_iterator end() const { return end_impl(); }

  reverse_iterator rbegin() { return reverse_iterator{end()}; }
  reverse_iterator rend() { return reverse_iterator{begin()}; }
  const_reverse_iterator rbegin() const { return const_reverse_iterator{end()}; }
  const_reverse_iterator rend() const { return const_reverse_iterator{begin()}; }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  iterator find(key_type key, bool exact_match = true) { return find_impl(key, exact_match); }
  const_iterator find(key_type key, bool exact_match = true) const { return find_impl(key, exact_match); }

  reverse_iterator rfind(key_type key, bool exact_match = true) { return rfind_impl(key, exact_match); }
  const_reverse_iterator rfind(key_type key, bool exact_match = true) const { return rfind_impl(key, exact_match); }

  bool insert(const iterator::value_type& key_value);
  bool insert(const RTree::const_iterator& it_start, const RTree::const_iterator& it_end);
  void erase(const const_iterator& pos, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);
  bool erase(key_type key, std::vector<FreeBlocksRangeInfo>& blocks_to_delete);

 private:
  uint32_t AllocBlockForTree(uint32_t near, std::vector<FreeBlocksExtentInfo>& allocated);

  iterator begin_impl() const;
  iterator end_impl() const;
  iterator find_impl(key_type key, bool exact_match = true) const;
  reverse_iterator rfind_impl(key_type key, bool exact_match = true) const;

  FreeBlocksAllocator* allocator_;
};
