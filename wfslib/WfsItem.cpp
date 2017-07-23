/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "WfsItem.h"
#include "Structs.h"
#include "MetadataBlock.h"

WfsItem::WfsItem(const std::string& name, const AttributesBlock& attributes) : name(name), attributes(attributes) {}

Attributes * AttributesBlock::Attributes() const {
	if (!block) return NULL;
	return reinterpret_cast<::Attributes *>(&block->GetData()[attributes_offset]);
}

bool WfsItem::IsDirectory() {
	auto attributes = this->attributes.Attributes();
	return !attributes->IsLink() && attributes->IsDirectory();
}

bool WfsItem::IsFile() {
	auto attributes = this->attributes.Attributes();
	return !attributes->IsLink() && !attributes->IsDirectory();
}

bool WfsItem::IsLink() {
	auto attributes = this->attributes.Attributes();
	return attributes->IsLink();
}
