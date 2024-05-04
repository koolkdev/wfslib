/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_iterator.h"

#include "quota_area.h"
#include "wfs_item.h"

DirectoryIterator::DirectoryIterator(std::shared_ptr<QuotaArea> quota,
                                     std::deque<parent_node_info> parents,
                                     leaf_node_info leaf)
    : quota_(quota), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

DirectoryIterator::reference DirectoryIterator::operator*() const {
  auto val = *leaf_.iterator;
  return {val.key, WfsItem::Load(quota_, val.key, attributes())};
}

DirectoryIterator& DirectoryIterator::operator++() {
  assert(!is_end());
  if ((++leaf_.iterator).is_end()) {
    std::deque<parent_node_info> removed_parents;
    while (!parents_.empty() && (++parents_.back().iterator).is_end()) {
      removed_parents.push_back(std::move(parents_.back()));
      parents_.pop_back();
    }
    if (parents_.empty()) {
      // end
      for (auto& parent : removed_parents) {
        --parent.iterator;
        removed_parents.push_front(std::move(parent));
      }
      return *this;
    }
    auto current_block = throw_if_error(quota_->LoadMetadataBlock((*parents_.back().iterator).value));
    while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
             MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
      parents_.push_back({std::move(current_block)});
      parents_.back().iterator = parents_.back().node->begin();
      assert(!parents_.back().iterator.is_end());
      current_block = throw_if_error(quota_->LoadMetadataBlock((*parents_.back().iterator).value));
    }
    leaf_ = {std::move(current_block)};
    leaf_.iterator = leaf_.node->begin();
    assert(!leaf_.iterator.is_end());
  }
  return *this;
}

DirectoryIterator& DirectoryIterator::operator--() {
  assert(!is_begin());
  if (leaf_.iterator.is_begin()) {
    std::deque<parent_node_info> removed_parents;
    while (!parents_.empty() && parents_.back().iterator.is_begin()) {
      removed_parents.push_back(std::move(parents_.back()));
      parents_.pop_back();
    }
    if (parents_.empty()) {
      // begin
      for (auto& parent : removed_parents) {
        removed_parents.push_front(std::move(parent));
      }
      return *this;
    }
    auto current_block = throw_if_error(quota_->LoadMetadataBlock((*parents_.back().iterator).value));
    while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
             MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
      parents_.push_back({std::move(current_block)});
      parents_.back().iterator = parents_.back().node->end();
      assert(!parents_.back().iterator.is_begin());
      current_block = throw_if_error(quota_->LoadMetadataBlock((*--parents_.back().iterator).value));
    }
    leaf_ = {std::move(current_block)};
    leaf_.iterator = leaf_.node->end();
    assert(!leaf_.iterator.is_begin());
  }
  --leaf_.iterator;
  return *this;
}

DirectoryIterator DirectoryIterator::operator++(int) {
  DirectoryIterator tmp(*this);
  ++(*this);
  return tmp;
}

DirectoryIterator DirectoryIterator::operator--(int) {
  DirectoryIterator tmp(*this);
  --(*this);
  return tmp;
}

bool DirectoryIterator::is_begin() const {
  return leaf_.iterator.is_begin() &&
         std::ranges::all_of(parents_, [](const auto& node) { return node.iterator.is_begin(); });
}
