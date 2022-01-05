/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "Directory.h"
#include "Structs.h"
#include "MetadataBlock.h"
#include "Area.h"
#include "File.h"
#include "Link.h"
#include "SubBlockAllocator.h"

std::shared_ptr<WfsItem> Directory::GetObject(const std::string& name) {
	AttributesBlock attributes_block = GetObjectAttributes(block, name);
	if (!attributes_block.block) return std::shared_ptr<WfsItem>();
	return Create(name, attributes_block);
}

std::shared_ptr<Directory> Directory::GetDirectory(const std::string& name) {
	AttributesBlock attributes_block = GetObjectAttributes(block, name);
	auto attributes = attributes_block.Attributes();
	if (!attributes || attributes->IsLink() || !attributes->IsDirectory()) {
		// Not found / Not a directory
		return std::shared_ptr<Directory>();
	}
	return std::dynamic_pointer_cast<Directory>(Create(name, attributes_block));
}

std::shared_ptr<File> Directory::GetFile(const std::string& name) {
	AttributesBlock attributes_block = GetObjectAttributes(block, name);
	auto attributes = attributes_block.Attributes();
	if (!attributes || attributes->IsLink() || attributes->IsDirectory()) {
		// Not found / Not a file
		return std::shared_ptr<File>();
	}
	return std::dynamic_pointer_cast<File>(Create(name, attributes_block));
}

std::shared_ptr<WfsItem> Directory::Create(const std::string& name, const AttributesBlock& attributes_block) {
	auto attributes = attributes_block.Attributes();
	if (attributes->IsLink()) {
		// TODO, I think that the link info is in the attributes metadata
		return std::make_shared<Link>(name, attributes_block, area);
	}
	else if (attributes->IsDirectory()) {
		if (attributes->flags.value() & attributes->Flags::QUOTA) {
			// The directory is quota, aka new area
			auto block_size = Block::BlockSize::Basic;
			if (!(attributes->flags.value() & attributes->Flags::AREA_SIZE_BASIC) && (attributes->flags.value() & attributes->Flags::AREA_SIZE_REGULAR))
				block_size = Block::BlockSize::Regular;
			auto new_area = area->GetArea(attributes->directory_block_number.value(), name, attributes_block, block_size);
			return new_area->GetRootDirectory();
		}
		else {
			return area->GetDirectory(attributes->directory_block_number.value(), name, attributes_block);
		}
	}
	else {
		// IsFile()
		return std::make_shared<File>(name, attributes_block, area);
	}
}

AttributesBlock Directory::GetObjectAttributes(const std::shared_ptr<MetadataBlock>& block, const std::string& name) {
	SubBlockAllocator allocator(block);
	auto current_node = allocator.GetRootNode<DirectoryTreeNode>();
	if (block->Header()->block_flags.value() & block->Header()->Flags::EXTERNAL_DIRECTORY_TREE) {
		auto pos_in_path = name.begin();
		while (true) {
			ExternalDirectoryTreeNode * external_node = static_cast<ExternalDirectoryTreeNode *>(current_node);
			if (current_node->value_length.value() > name.end() - pos_in_path) {
				// not equal.. path too long
				return AttributesBlock();
			}
			if (current_node->value_length.value() && std::strncmp(&*pos_in_path, current_node->value().c_str(), current_node->value_length.value())) {
				// not equal.. not found
				return AttributesBlock();
			}
			pos_in_path += current_node->value_length.value();
			char next_expected_char = 0;
			if (pos_in_path < name.end()) next_expected_char = *pos_in_path;
			auto choices = current_node->choices();
			// This is sorted list, so we can find it with lower_bound
			auto res = std::lower_bound(choices.begin(), choices.end(), next_expected_char);
			if (res == choices.end() || *res != next_expected_char) {
				// Not found
				return AttributesBlock();
			}
			auto value_offset = external_node->get_item(res - choices.begin()).value();
			if (pos_in_path == name.end()) {
				// We found the attribute!
				return { block, value_offset };
			}
			pos_in_path++;
			// Go to next node
			current_node = allocator.GetNode<DirectoryTreeNode>(value_offset);
		}
	}
	else {
		// Arghh, trees over trees
		auto node_state = std::make_shared<NodeState>(NodeState{ block, current_node, std::shared_ptr<NodeState>(), 0, current_node->value() });
		// -- because it will be advanced immedialty to 0 when we do ++
		--node_state->current_index;
		uint32_t last_block_number = 0;
		while (true) {
			while (++(node_state->current_index) == node_state->node->choices_count.value()) {
				node_state = std::move(node_state->parent);
				if (!node_state) break;
			}
			if (!node_state) break;
			// Enter nodes until we hit a node that has value
			while (node_state->node->choices()[node_state->current_index]) {
				auto node_block = node_state->block;
				uint16_t node_offset = 0;
				node_offset = static_cast<InternalDirectoryTreeNode *>(node_state->node)->get_item(node_state->current_index).value();
				current_node = allocator.GetNode<DirectoryTreeNode>(node_offset);
				std::string path = node_state->path + std::string(1, node_state->node->choices()[node_state->current_index]) + current_node->value();
				node_state = std::make_shared<NodeState>(NodeState{ node_block, current_node, std::move(node_state), 0, path });
			}
			// Check if our string is lexicographic smaller
			if (node_state->path.size() && std::strncmp(&*name.begin(), &*node_state->path.begin(), std::min(name.size(), node_state->path.size())) < 0) break;
			last_block_number = static_cast<InternalDirectoryTreeNode  *>(node_state->node)->get_next_allocator_block_number().value();
			if (node_state->path == name) break; // No need to continue with the search
		}
		if (!last_block_number) {
			// Not found
			return AttributesBlock();
		}
		return GetObjectAttributes(area->GetMetadataBlock(last_block_number), name);
	}
}

size_t Directory::GetItemsCount() {
	return std::distance(begin(), end());
}

Directory::FilesIterator& Directory::FilesIterator::operator++() {
	// Go back until we are not at the end of a node
	while (++(node_state->current_index) == node_state->node->choices_count.value()) {
		node_state = std::move(node_state->parent);
		if (!node_state) return *this;
	}
	// Enter nodes until we hit a node that has value
	while (node_state->node->choices()[node_state->current_index]) {
		auto block = node_state->block;
		uint16_t node_offset = 0;
		if (node_state->block->Header()->block_flags.value() & node_state->block->Header()->Flags::EXTERNAL_DIRECTORY_TREE) {
			node_offset = static_cast<ExternalDirectoryTreeNode *>(node_state->node)->get_item(node_state->current_index).value();
		}
		else {
			node_offset = static_cast<InternalDirectoryTreeNode  *>(node_state->node)->get_item(node_state->current_index).value();
		}
		auto current_node = SubBlockAllocator(block).GetNode<DirectoryTreeNode>(node_offset);
		std::string path = node_state->path + std::string(1, node_state->node->choices()[node_state->current_index]) + current_node->value();
		node_state = std::make_shared<NodeState>(NodeState { block, current_node, std::move(node_state), 0, path });
	}
	if (!(node_state->block->Header()->block_flags.value() & node_state->block->Header()->Flags::EXTERNAL_DIRECTORY_TREE)) {
		// This is just internal node (in the directories trees tree), it just point to another tree
		auto block = directory->area->GetMetadataBlock(static_cast<InternalDirectoryTreeNode  *>(node_state->node)->get_next_allocator_block_number().value());
		auto current_node = SubBlockAllocator(block).GetRootNode<DirectoryTreeNode>();
		node_state = std::make_shared<NodeState>(NodeState{ block, current_node, std::move(node_state), 0, current_node->value() });
		// -- because it will be advanced immedialty to 0 when we do ++
		--node_state->current_index;
		// Go to the first node in this directory block
		return operator++();
	}
	return *this;
}

std::shared_ptr<WfsItem> Directory::FilesIterator::operator*() {
	if (!node_state) return std::shared_ptr<WfsItem>();
	if (node_state->block->Header()->block_flags.value() & node_state->block->Header()->Flags::EXTERNAL_DIRECTORY_TREE) {
		auto block = node_state->block;
		auto external_node = static_cast<ExternalDirectoryTreeNode *>(node_state->node);
		return directory->Create(node_state->path, AttributesBlock { block, external_node->get_item(node_state->current_index).value() });
	}
	else {
		// Should not happen (can't happen, the iterator should stop only at external trees)
		throw std::logic_error("Should not happen!");
	}
}

Directory::FilesIterator Directory::begin() {
	auto current_node = SubBlockAllocator(block).GetRootNode<DirectoryTreeNode>();
	if (!current_node->choices_count.value()) return end();
	auto node_state = std::make_shared<NodeState>(NodeState{ block, current_node, std::shared_ptr<NodeState>(), 0, current_node->value() });
	// -- because it will be advanced immedialty to 0 when we do ++
	--node_state->current_index;
	auto res = FilesIterator(shared_from_this(), std::move(node_state));
	++res;
	return res;
}

Directory::FilesIterator Directory::end() {
	return FilesIterator(shared_from_this(), std::shared_ptr<NodeState>());
}