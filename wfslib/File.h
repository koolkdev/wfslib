/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <memory>
#include <vector>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/positioning.hpp>

#include "WfsItem.h"
#include "DataBlock.h"

class Area;
class MetadataBlock;
struct DataBlocksClusterMetadata;

class File : public WfsItem, public std::enable_shared_from_this<File> {
private:
	// TODO: We may have cyclic reference here if we do cache in area.
	std::shared_ptr<Area> area;

	class DataCategoryReader;
	class DataCategory0Reader;
	class RegularDataCategoryReader;
	class DataCategory1Reader;
	class DataCategory2Reader;
	class DataCategory3Reader;
	class DataCategory4Reader;

	static std::shared_ptr<DataCategoryReader> CreateReader(const std::shared_ptr<File>& file);

public:
	File(const std::string& name, const AttributesBlock& attributes, const std::shared_ptr<Area>& area) : WfsItem(name, attributes), area(area) {
	}

	uint32_t GetSize();

	class file_device {
	public:
		typedef char char_type;
		typedef boost::iostreams::seekable_device_tag category;
		file_device(const std::shared_ptr<File>& file);

		std::streamsize read(char_type* s, std::streamsize n);
		std::streamsize write(const char_type* s, std::streamsize n);
		boost::iostreams::stream_offset seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way);

	private:
		size_t size();
		std::shared_ptr<File> file;
		std::shared_ptr<DataCategoryReader> reader;
		boost::iostreams::stream_offset pos;
	};

	typedef boost::iostreams::stream<file_device> stream;
};
