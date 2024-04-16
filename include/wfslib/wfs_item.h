/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <string>

struct Attributes;
struct AttributesBlock;
class Block;

// TODO: change to AttributesRef
struct AttributesBlock {
  std::shared_ptr<Block> block;
  size_t attributes_offset;

  ::Attributes* mutable_data();
  const ::Attributes* data() const;
};

class WfsItem {
 public:
  WfsItem(const std::string& name, const AttributesBlock& block);
  virtual ~WfsItem() {}
  const std::string& GetName() const { return name_; }
  virtual bool IsDirectory() const;
  virtual bool IsFile() const;
  virtual bool IsLink() const;
  virtual bool IsQuota() const;

 protected:
  AttributesBlock& attributes_data() { return attributes_; }
  const AttributesBlock& attributes_data() const { return attributes_; }

  std::string name_;
  AttributesBlock attributes_;
};
