/*
 * Copyright (C) 2024 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "directory_map_iterator.h"

#include "quota_area.h"

DirectoryMapIterator::DirectoryMapIterator(std::shared_ptr<QuotaArea> quota,
                                           std::vector<parent_node_info> parents,
                                           leaf_node_info leaf)
    : quota_(quota), parents_(std::move(parents)), leaf_(std::move(leaf)) {}

DirectoryMapIterator::reference DirectoryMapIterator::operator*() const {
  auto val = *leaf_.iterator;
  return {val.key(), {leaf_.node.block(), uint16_t{val.value()}}};
}

DirectoryMapIterator& DirectoryMapIterator::operator++() {
  assert(!is_end());
  if ((++leaf_.iterator).is_end()) {
    std::vector<parent_node_info> removed_parents;
    while (!parents_.empty() && (++parents_.back().iterator).is_end()) {
      removed_parents.push_back(std::move(parents_.back()));
      parents_.pop_back();
    }
    if (parents_.empty()) {
      // end
      for (auto& parent : std::views::reverse(removed_parents)) {
        --parent.iterator;
        parents_.push_back(std::move(parent));
      }
      return *this;
    }
    auto current_block = throw_if_error(quota_->LoadMetadataBlock((*parents_.back().iterator).value()));
    while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
             MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
      parents_.push_back({std::move(current_block)});
      parents_.back().iterator = parents_.back().node.begin();
      assert(!parents_.back().iterator.is_end());
      current_block = throw_if_error(quota_->LoadMetadataBlock((*parents_.back().iterator).value()));
    }
    leaf_ = {std::move(current_block)};
    leaf_.iterator = leaf_.node.begin();
    assert(!leaf_.iterator.is_end());
  }
  return *this;
}

DirectoryMapIterator& DirectoryMapIterator::operator--() {
  assert(!is_begin());
  if (leaf_.iterator.is_begin()) {
    std::vector<parent_node_info> removed_parents;
    while (!parents_.empty() && parents_.back().iterator.is_begin()) {
      removed_parents.push_back(std::move(parents_.back()));
      parents_.pop_back();
    }
    if (parents_.empty()) {
      // begin
      for (auto& parent : std::views::reverse(removed_parents)) {
        parents_.push_back(std::move(parent));
      }
      return *this;
    }
    auto current_block = throw_if_error(quota_->LoadMetadataBlock((*--parents_.back().iterator).value()));
    while (!(current_block->get_object<MetadataBlockHeader>(0)->block_flags.value() &
             MetadataBlockHeader::Flags::DIRECTORY_LEAF_TREE)) {
      parents_.push_back({std::move(current_block)});
      parents_.back().iterator = parents_.back().node.end();
      assert(!parents_.back().iterator.is_begin());
      current_block = throw_if_error(quota_->LoadMetadataBlock((*--parents_.back().iterator).value()));
    }
    leaf_ = {std::move(current_block)};
    leaf_.iterator = leaf_.node.end();
    assert(!leaf_.iterator.is_begin());
  }
  --leaf_.iterator;
  return *this;
}

DirectoryMapIterator DirectoryMapIterator::operator++(int) {
  DirectoryMapIterator tmp(*this);
  ++(*this);
  return tmp;
}

DirectoryMapIterator DirectoryMapIterator::operator--(int) {
  DirectoryMapIterator tmp(*this);
  --(*this);
  return tmp;
}

bool DirectoryMapIterator::is_begin() const {
  return leaf_.iterator.is_begin() &&
         std::ranges::all_of(parents_, [](const auto& node) { return node.iterator.is_begin(); });
}
