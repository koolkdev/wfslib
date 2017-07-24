/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "File.h"
#include "Structs.h"
#include "MetadataBlock.h"
#include "DataBlock.h"
#include "Area.h"

uint32_t File::GetSize() {
	return attributes.Attributes()->size.value();
}

static size_t GetOffsetInMetadataBlock(const std::shared_ptr<MetadataBlock>& block, uint8_t* data) {
	return data - &block->GetData()[0];
}

class File::DataCategoryReader {
public:
	DataCategoryReader(const std::shared_ptr<File>& file) : file(file) {}
	virtual size_t GetAttributesMetadataSize() = 0;
	virtual size_t Read(uint8_t* data, size_t offset, size_t size) = 0;
protected:
	std::shared_ptr<File> file;

	std::vector<uint8_t>::iterator GetAttributesMetadata() {
		return file->attributes.block->GetData().begin() + file->attributes.attributes_offset + file->attributes.Attributes()->DataOffset();
	}
	std::vector<uint8_t>::iterator GetAttributesMetadataEnd() {
		return file->attributes.block->GetData().begin() + file->attributes.attributes_offset + round_pow2(file->attributes.Attributes()->DataOffset() + GetAttributesMetadataSize());
	}
};

// Category 0 - File data is in the attribute metadata (limited to 512 bytes minus attribute size) (no minumum)
class File::DataCategory0Reader : public File::DataCategoryReader {
public:
	DataCategory0Reader(const std::shared_ptr<File>& file) : DataCategoryReader(file) {}

	virtual size_t GetAttributesMetadataSize() {
		return file->attributes.Attributes()->size_on_disk.value();
	}

	virtual size_t Read(uint8_t* data, size_t offset, size_t size) {
		auto data_begin = GetAttributesMetadata();
		std::copy(data_begin + offset, data_begin + offset + size, data);
		return size;
	}
};

class File::RegularDataCategoryReader : public File::DataCategoryReader {
public:
	RegularDataCategoryReader(const std::shared_ptr<File>& file) : DataCategoryReader(file) {}

	virtual size_t GetAttributesMetadataSize() {
		// round up dividation
		size_t data_blocks_count = ((file->attributes.Attributes()->size_on_disk.value() - 1) >> GetDataBlockSize()) + 1;
		return sizeof(DataBlockMetadata) * data_blocks_count;
	}

	virtual size_t Read(uint8_t* data, size_t offset, size_t size) {
		auto blocks_list = reinterpret_cast<DataBlockMetadata *>(&*GetAttributesMetadataEnd());
		int64_t block_index = offset >> GetDataBlockSize();
		size_t offset_in_block = offset & ((1 << GetDataBlockSize()) - 1);
		auto hash_block = file->attributes.block;

		LoadDataBlock(blocks_list[-block_index - 1].block_number.value(),
			static_cast<uint32_t>(std::min(1U << GetDataBlockSize(), file->attributes.Attributes()->size.value() - static_cast<uint32_t>(offset))),
			DataBlock::DataBlockHash{ hash_block, GetOffsetInMetadataBlock(hash_block, reinterpret_cast<uint8_t*>(&blocks_list[-block_index - 1].hash)) });
		auto& block_data = current_data_block->GetData();
		size = std::min(size, block_data.size() - offset_in_block);
		std::copy(block_data.begin() + offset_in_block, block_data.begin() + offset_in_block + size, data);
		return size;
	}

protected:
	virtual size_t GetBlocksLog2CountInDataBlock() = 0;
	virtual Block::BlockSize GetDataBlockSize() {
		return static_cast<Block::BlockSize>(file->area->GetDataBlockLog2Size() + GetBlocksLog2CountInDataBlock());
	}
	std::shared_ptr<DataBlock> current_data_block;

	void LoadDataBlock(uint32_t block_number, uint32_t data_size, const DataBlock::DataBlockHash& data_hash) {
		if (current_data_block && file->area->GetBlockNumber(current_data_block) == block_number) return;
		current_data_block = file->area->GetDataBlock(block_number, GetDataBlockSize(), data_size, data_hash);
	}
};

// Category 1 - File data in regluar blocks, in the attribute metadata there is a reversed list of block numbers and hashes. Limited to 5 blocks. (no minumum)
class File::DataCategory1Reader : public File::RegularDataCategoryReader {
public:
	DataCategory1Reader(const std::shared_ptr<File>& file) : RegularDataCategoryReader(file) {}
protected:
	virtual size_t GetBlocksLog2CountInDataBlock() {
		return 0;
	}
};

// Category 2 - File data in mega block (8 regular blocks), in the attribute metadata there is a reversed list of block numbers and hashes. Limited to 5 mega blocks. (minimum size of more than 1 regular block)
class File::DataCategory2Reader : public File::RegularDataCategoryReader {
public:
	DataCategory2Reader(const std::shared_ptr<File>& file) : RegularDataCategoryReader(file) {}
protected:
	virtual size_t GetBlocksLog2CountInDataBlock() {
		return 3;
	}
};

// Category 3 - File data in clusters of mega block (8 mega blocksblocks), in the attribute metadata there is a reversed list of block number and 8 hashes for each cluster. Limited to 4 clusters. (minimum size of more than 1 mega block)
class File::DataCategory3Reader : public File::DataCategory2Reader {
public:
	DataCategory3Reader(const std::shared_ptr<File>& file) : DataCategory2Reader(file) {}

	virtual size_t GetAttributesMetadataSize() {
		size_t data_blocks_clusters_count = ((file->attributes.Attributes()->size_on_disk.value() - 1) >> ClusterDataLog2Size()) + 1;
		return sizeof(DataBlocksClusterMetadata) * data_blocks_clusters_count;
	}

	virtual size_t Read(uint8_t* data, size_t offset, size_t size) {
		return ReadFromClustersList(data, offset, offset, size, file->attributes.block, reinterpret_cast<DataBlocksClusterMetadata *>(&*GetAttributesMetadataEnd()), true);
	}
protected:
	size_t ReadFromClustersList(uint8_t* data, size_t offset, size_t original_offset, size_t size, const std::shared_ptr<MetadataBlock>& metadata_block, DataBlocksClusterMetadata * clusters_list, bool reverse) {
		int64_t cluster_index = offset >> ClusterDataLog2Size();
		size_t offset_in_cluster = offset & ((1 << ClusterDataLog2Size()) - 1);
		size_t block_index = offset_in_cluster >> GetDataBlockSize();
		size_t offset_in_block = offset_in_cluster & ((1 << GetDataBlockSize()) - 1);

		DataBlocksClusterMetadata * cluster = NULL;
		if (reverse)
			cluster = &clusters_list[-cluster_index - 1];
		else
			cluster = &clusters_list[cluster_index];
		LoadDataBlock(cluster->block_number.value() + static_cast<uint32_t>(block_index << GetBlocksLog2CountInDataBlock()),
			static_cast<uint32_t>(std::min(1U << GetDataBlockSize(), file->attributes.Attributes()->size.value() - static_cast<uint32_t>(original_offset))),
			DataBlock::DataBlockHash{ metadata_block, GetOffsetInMetadataBlock(metadata_block, reinterpret_cast<uint8_t*>(&cluster->hash[block_index])) });
		auto& block_data = current_data_block->GetData();
		size = std::min(size, block_data.size() - offset_in_block);
		std::copy(block_data.begin() + offset_in_block, block_data.begin() + offset_in_block + size, data);
		return size;
	}

	size_t ClusterDataLog2Size() {
		return GetDataBlockSize() + 3;
	}
};

// Category 4 - File data in clusters of mega block (8 mega blocksblocks), in the attribute metadata there is list of block numbers of metadata block with lists of block number and 8 hashes for each cluster. Limited to 237 metadata blocks of lists. (max file size) (minumum size of more/equal than 1 cluster)
class File::DataCategory4Reader : public File::DataCategory3Reader {
public:
	DataCategory4Reader(const std::shared_ptr<File>& file) : DataCategory3Reader(file) {}

	virtual size_t GetAttributesMetadataSize() {
		size_t data_blocks_clusters_count = ((file->attributes.Attributes()->size_on_disk.value() - 1) >> ClusterDataLog2Size()) + 1;
		size_t blocks_count = ((data_blocks_clusters_count - 1) / ClustersInBlock()) + 1;
		return sizeof(boost::endian::big_uint32_buf_t) * blocks_count;
	}

	virtual size_t Read(uint8_t* data, size_t offset, size_t size) {
		auto blocks_list = reinterpret_cast<boost::endian::big_uint32_buf_t *>(&*GetAttributesMetadataEnd());
		int64_t block_index = offset / (ClustersInBlock() << ClusterDataLog2Size());
		size_t offset_in_block = offset % (ClustersInBlock() << ClusterDataLog2Size());
		LoadMetadataBlock(blocks_list[-block_index - 1].value());
		return ReadFromClustersList(data, offset_in_block, offset, size, current_metadata_block,
			reinterpret_cast<DataBlocksClusterMetadata *>(&*(current_metadata_block->GetData().begin() + sizeof(MetadataBlockHeader))), false);
	}

protected:
	std::shared_ptr<MetadataBlock> current_metadata_block;

	void LoadMetadataBlock(uint32_t block_number) {
		if (current_metadata_block && file->area->GetBlockNumber(current_metadata_block) == block_number) return;
		current_metadata_block = file->area->GetMetadataBlock(block_number);
	}

	size_t ClustersInBlock() {
		size_t clusters_in_block = (file->area->GetDataBlockLog2Size() - sizeof(MetadataBlockHeader)) / sizeof(DataBlocksClusterMetadata);
		clusters_in_block = std::min(clusters_in_block, static_cast<size_t>(48));
		return clusters_in_block;
	}
};

std::shared_ptr<File::DataCategoryReader> File::CreateReader(const std::shared_ptr<File>& file) {
	switch (file->attributes.Attributes()->size_category.value()) {
	case 0: return std::make_shared<DataCategory0Reader>(file);
	case 1: return std::make_shared<DataCategory1Reader>(file);
	case 2: return std::make_shared<DataCategory2Reader>(file);
	case 3: return std::make_shared<DataCategory3Reader>(file);
	case 4: return std::make_shared<DataCategory4Reader>(file);
	default: throw std::runtime_error("Unexpected file category");
	}
}

File::file_device::file_device(const std::shared_ptr<File>& file) : file(file), pos(0), reader(std::move(CreateReader(file))) {
}

size_t File::file_device::size() {
	return file->attributes.Attributes()->size.value();
}

std::streamsize File::file_device::read(char_type* s, std::streamsize n)
{
	std::streamsize amt = static_cast<std::streamsize>(size() - pos);
	std::streamsize result = std::min(n, amt);

	if (result <= 0) return -1; // EOF

	std::streamsize to_read = result;
	while (to_read > 0) {
		size_t read = reader->Read(s, static_cast<size_t>(pos), static_cast<size_t>(to_read));
		s += read;
		pos += read;
		to_read -= read;
	}
	return result;
}
std::streamsize File::file_device::write(const char_type* s, std::streamsize n)
{
	return -1; // Not implemented
}
boost::iostreams::stream_offset File::file_device::seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way)
{
	// Determine new value of pos_
	boost::iostreams::stream_offset next;
	if (way == std::ios_base::beg) {
		next = off;
	}
	else if (way == std::ios_base::cur) {
		next = pos + off;
	}
	else if (way == std::ios_base::end) {
		next = size() + off - 1;
	}
	else {
		throw std::ios_base::failure("bad seek direction");
	}

	// Check for errors
	if (next < 0 || next >= static_cast<boost::iostreams::stream_offset>(size()))
		throw std::ios_base::failure("bad seek offset");

	pos = next;
	return pos;
}
