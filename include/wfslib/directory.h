/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <string>
#include "wfs_item.h"

class Area;
class MetadataBlock;
class File;

struct DirectoryTreeNode;

class Directory : public WfsItem, public std::enable_shared_from_this<Directory> {
public:
	Directory(const std::string& name, AttributesBlock attributes, const std::shared_ptr<Area>& area, const std::shared_ptr<MetadataBlock>& block) : WfsItem(name, attributes), area_(area), block_(block) {
	}

	std::shared_ptr<WfsItem> GetObject(const std::string& name);
	std::shared_ptr<Directory> GetDirectory(const std::string& name);
	std::shared_ptr<File> GetFile(const std::string& name);

	size_t GetItemsCount();

private:
	const std::shared_ptr<Area>& area() const { return area_; }

	// TODO: We may have cyclic reference here if we do cache in area.
	std::shared_ptr<Area> area_;

	std::shared_ptr<MetadataBlock> block_;

	std::shared_ptr<WfsItem> Create(const std::string& name, const AttributesBlock& attributes);
	AttributesBlock GetObjectAttributes(const std::shared_ptr<MetadataBlock>& block, const std::string& name);

	struct NodeState {
		std::shared_ptr<MetadataBlock> block;
		DirectoryTreeNode * node;
		std::shared_ptr<NodeState> parent;
		size_t current_index;
		std::string path;
	};

public:
	class FilesIterator : public std::iterator<std::input_iterator_tag, std::shared_ptr<WfsItem>> {
	private:
		std::shared_ptr<Directory> directory_;
		std::shared_ptr<NodeState> node_state_;
	public:
		FilesIterator(const std::shared_ptr<Directory>& directory, const std::shared_ptr<NodeState>& node_state) : directory_(directory), node_state_(std::move(node_state)) {}
		FilesIterator(const FilesIterator& mit) : directory_(mit.directory_), node_state_(mit.node_state_) {}
		FilesIterator& operator++();
		FilesIterator operator++(int) { FilesIterator tmp(*this); operator++(); return tmp; }
		bool operator==(const FilesIterator& rhs) const {
			if (!rhs.node_state_ && !node_state_) return true;
			if (!rhs.node_state_ != !node_state_) return false;
			return rhs.node_state_->block == node_state_->block &&
				rhs.node_state_->node == node_state_->node &&
				rhs.node_state_->current_index == node_state_->current_index;
		}
		bool operator!=(const FilesIterator& rhs) const { return !operator==(rhs); }
		std::shared_ptr<WfsItem> operator*();
	};

	FilesIterator begin();
	FilesIterator end();
};