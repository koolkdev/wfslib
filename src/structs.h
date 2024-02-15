/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <boost/endian/buffers.hpp>
#include <span>
#include <string>
#include <vector>

// sizeof 0x18
struct MetadataBlockHeader {
  // Flags:
  // 0x00400000 - Area header
  // 0x00800000 - Root area
  // 0x20000000 - leaf directory tree
  // 0x40000000 - ?
  // 0x80000000 - Directory?
  enum Flags {
    AREA = 0x00400000,
    ROOT_AREA = 0x00800000,
    EXTERNAL_DIRECTORY_TREE = 0x20000000,
    DIRECTORY = 0x80000000,
  };
  boost::endian::big_uint32_buf_t block_flags;  // 20 least bits ignored
  boost::endian::big_uint8_buf_t hash[20];
};
static_assert(sizeof(MetadataBlockHeader) == 0x18, "Incorrect sizeof MetadataBlockHeader");

// sizeof 0x18
struct DataBlockMetadata {
  boost::endian::big_uint32_buf_t block_number;
  boost::endian::big_uint8_buf_t hash[20];
};
static_assert(sizeof(DataBlockMetadata) == 0x18, "Incorrect sizeof DataBlockMetadata");

// sizeof 0xa4
struct DataBlocksClusterMetadata {
  boost::endian::big_uint32_buf_t block_number;
  boost::endian::big_uint8_buf_t hash[8][20];
};
static_assert(sizeof(DataBlocksClusterMetadata) == 0xa4, "Incorrect sizeof DataBlocksClusterMetadata");

// sizeof 0xc
struct Permissions {
  boost::endian::big_uint32_buf_t owner;
  boost::endian::big_uint32_buf_t group;
  boost::endian::big_uint32_buf_t mode;
};
static_assert(sizeof(Permissions) == 0xc, "Incorrect sizeof Permissions");

// sizeof 0x2c
struct Attributes {
  enum Flags {
    UNENCRYPTED_FILE = 0x2000000,
    LINK = 0x4000000,
    AREA_SIZE_BASIC = 0x10000000,
    AREA_SIZE_REGULAR = 0x20000000,
    QUOTA = 0x40000000,
    DIRECTORY = 0x80000000,
  };
  boost::endian::big_uint32_buf_t flags;
  boost::endian::big_uint32_buf_t size_on_disk;
  boost::endian::big_uint32_buf_t ctime;
  boost::endian::big_uint32_buf_t mtime;
  boost::endian::big_uint32_buf_t unknown;  // TODO: ????
  union {
    boost::endian::big_uint32_buf_t size;          // for file
    boost::endian::big_uint32_buf_t blocks_count;  // for quota
  };
  boost::endian::big_uint32_buf_t directory_block_number;  // in case of directory
  Permissions permissions;
  boost::endian::big_uint8_buf_t entry_log2_size;  // log2 size of the whole entry, including this attributes
  boost::endian::big_uint8_buf_t size_category;    // 0-4, see File.c
  boost::endian::big_uint8_buf_t filename_length;
  boost::endian::big_uint8_buf_t case_bitmap;  // This byte in the struct also behave as padding, it isn't really a
                                               // byte, it is a bitmap of filename_length

  bool IsDirectory() const { return !!(flags.value() & Flags::DIRECTORY); }
  bool IsFile() const { return !IsDirectory(); }
  bool IsLink() const { return !!(flags.value() & Flags::LINK); }
  bool IsQuota() const { return !!(flags.value() & Flags::QUOTA); }

  size_t DataOffset() const;

  std::string GetCaseSensitiveName(const std::string& name) const;
};
static_assert(sizeof(Attributes) == 0x2C, "Incorrect sizeof Attributes");

// sizeof 0x48
struct WfsHeader {
  boost::endian::big_uint32_buf_t iv;           // most 2 bits are device type. 1-mlc/3-usb
  boost::endian::big_uint32_buf_t version;      // should be 0x01010800
  boost::endian::big_uint16_buf_t device_type;  // usb - 0x16a2. mlc - 0x136a?
  boost::endian::big_uint16_buf_t _pad;
  Attributes root_area_attributes;
  boost::endian::big_uint32_buf_t
      transactions_area_block_number;  // must be 6 or 12 (6*2, when regular block size used)
  boost::endian::big_uint32_buf_t root_area_blocks_count;
  boost::endian::big_uint32_buf_t unknown[2];  // not used??
};
static_assert(sizeof(WfsHeader) == 0x48, "Incorrect sizeof WfsHeader");

// sizof 0x8
struct WfsAreaFragmentInfo {
  boost::endian::big_uint32_buf_t block_number;
  boost::endian::big_uint32_buf_t blocks_count;
};
static_assert(sizeof(WfsAreaFragmentInfo) == 0x8, "Incorrect sizeof WfsAreaFragmentInfo");

// sizeof 0x60
struct WfsAreaHeader {
  enum class AreaType : uint8_t {
    RootArea = 0,
    QuotaArea = 1,
  };
  boost::endian::big_uint32_buf_t iv;  // used for blocks  encryption
  boost::endian::big_uint32_buf_t blocks_count;
  boost::endian::big_uint32_buf_t root_directory_block_number;  // is 6
  // Those two directories are not used. It was supposed to be some kind of refcount mechansism. not really sure.
  // But nothing uses it in the WiiU
  boost::endian::big_uint32_buf_t shadow_directory_block_number_1;  // is 4
  boost::endian::big_uint32_buf_t shadow_directory_block_number_2;  // is 5
  boost::endian::big_uint8_buf_t depth;                             // how many total parents area this area has
  boost::endian::big_uint8_buf_t log2_block_size;                   // 12/13
  boost::endian::big_uint8_buf_t log2_mega_block_size;              // mega block - 8 blocks. so 15/16
  boost::endian::big_uint8_buf_t log2_mega_blocks_cluster_size;     // mega blocks cluster - 8 mega blocks. so 18/19
  boost::endian::big_uint8_buf_t area_type;          // 0 - root area, 1 - data (not relavent for transactions area)
  boost::endian::big_uint8_buf_t maybe_always_zero;  // init to zero. doesn't seem to be changed by anything
  boost::endian::big_uint16_buf_t
      spare_blocks_count;  // in case of quota, how many spare blocks this area allocated beyond the quota. (parent area
                           // blocks, so in parent area block size)
  WfsAreaFragmentInfo first_fragments[8];  // On which blocks this area is spread on (if there aren't enough free
                                           // sequentials blocks it will framgnet the area). parent area blocks, so in
                                           // parent area block size. This list contains only the first 8 fragments
  boost::endian::big_uint32_buf_t fragments_log2_block_size;  // block size of parent area
};
static_assert(sizeof(WfsAreaHeader) == 0x60, "Incorrect sizeof WfsAreaHeader");

// sizof 0x8
struct WfsAreaFragmentsInfo {
  boost::endian::big_uint16_buf_t max_fragments_count;  // 480
  boost::endian::big_uint16_buf_t
      fragments_log2_block_size;  // block size of parent area. In root area it is the minimum block size (12)
  boost::endian::big_uint32_buf_t fragments_count;
  WfsAreaFragmentInfo fragments[480];  // On which blocks this area is spread on (if there aren't enough free
                                       // sequentials blocks it will framgnet the area). parent area blocks, so in
                                       // parent area block size.
};
static_assert(sizeof(WfsAreaFragmentsInfo) == 0xF08, "Incorrect sizeof WfsAreaFragmentsInfo");

struct WfsQuotaArea {
  WfsAreaHeader header;

  WfsAreaFragmentsInfo framgnets;
};
static_assert(sizeof(WfsQuotaArea) == 0x60 + 0xF08, "Incorrect sizeof WfsQuotaArea");

struct WfsTransactionsArea {
  WfsAreaHeader header;

  // This is the area that responsible for creating transaction when making changes to the file system (until the quota
  // is flushed to the disk).
  // Most of the structures of this area are known, but it isn't interesting since it is cleared every time the WiiU
  // mount the file system. (transactions from previous boot that weren't flushed to the disk are lost)
  boost::endian::big_uint8_buf_t unknown[0x3B4];
};
static_assert(sizeof(WfsTransactionsArea) == 0x60 + 0x3B4, "Incorrect sizeof WfsTransactionsArea");

struct SubBlockAllocatorFreeListEntry {
  static const uint16_t FREE_MARK_CONST = 0xFEDC;

  boost::endian::big_uint16_buf_t free_mark;
  boost::endian::big_uint16_buf_t next;
  boost::endian::big_uint16_buf_t prev;
  boost::endian::big_uint16_buf_t log2_block_size;
};
static_assert(sizeof(SubBlockAllocatorFreeListEntry) == 0x8, "Incorrect sizeof SubBlockAllocatorFreeListEntry");

struct SubBlockAllocatorFreeList {
  boost::endian::big_uint16_buf_t free_blocks_count;
  boost::endian::big_uint16_buf_t head;  // head free block
};
static_assert(sizeof(SubBlockAllocatorFreeList) == 0x4, "Incorrect sizeof SubBlockAllocatorFreeList");

struct SubBlockAllocatorStruct {
  SubBlockAllocatorFreeList free_list[8];  // for sizes 1<<3-10
};
static_assert(sizeof(SubBlockAllocatorStruct) == 0x20, "Incorrect sizeof SubBlockAllocatorStruct");

struct DirectoryTreeHeader {
  boost::endian::big_uint16_buf_t root;
  boost::endian::big_uint16_buf_t records_count;
};
static_assert(sizeof(DirectoryTreeHeader) == 0x4, "Incorrect sizeofDirectoryTreeHeader");

struct DirectoryTreeNode {
  boost::endian::big_uint8_buf_t prefix_length;
  boost::endian::big_uint8_buf_t choices_count;

 public:
  std::string prefix() const { return {reinterpret_cast<const char*>(this + 1), prefix_length.value()}; }
  std::span<const std::byte> choices() const {
    return {reinterpret_cast<const std::byte*>(this + 1) + prefix_length.value(), choices_count.value()};
  }
};
static_assert(sizeof(DirectoryTreeNode) == 0x2, "Incorrect sizeof DirectoryTreeNode");

struct ExternalDirectoryTreeNode : DirectoryTreeNode {
 private:
  size_t size() const;

 public:
  boost::endian::big_uint16_buf_t get_item(size_t index) const {
    return reinterpret_cast<const boost::endian::big_uint16_buf_t*>(reinterpret_cast<const std::byte*>(this) +
                                                                    size())[-static_cast<int>(index) - 1];
  }
};

struct InternalDirectoryTreeNode : DirectoryTreeNode {
 private:
  size_t size() const;

 public:
  boost::endian::big_uint16_buf_t get_item(size_t index) const {
    return reinterpret_cast<const boost::endian::big_uint16_buf_t*>(
        reinterpret_cast<const std::byte*>(this) + size() -
        (choices()[0] == std::byte{0} ? 2 : 0))[-static_cast<int>(index) - 1];
  }
  boost::endian::big_uint32_buf_t get_next_allocator_block_number() const {
    return reinterpret_cast<const boost::endian::big_uint32_buf_t*>(reinterpret_cast<const std::byte*>(this) +
                                                                    size())[-1];
  }
};

struct FreeBlocksAllocatorHeader {
  boost::endian::big_uint32_buf_t free_blocks_count;
  boost::endian::big_uint32_buf_t unknown;  // initialized to 1
  // When createa a new area, a fixed amount of blocks are allocated for metadata blocks for quick allocation. When
  // allocating metadata blocks, it will advance |free_metadata_block| and will decrease |free_metadata_blocks_count|.
  // When no more availabe blocks are avaialbe, it will allocate them regulary.
  boost::endian::big_uint32_buf_t free_metadata_block;
  boost::endian::big_uint32_buf_t free_metadata_blocks_count;
};
static_assert(sizeof(FreeBlocksAllocatorHeader) == 0x10, "Incorrect sizeof FreeBLocksAllocatorHeader");

// Header at the end of the block. Contains an array of entries of a constant size
struct TreeSubBlockAllocatorHeader {
  boost::endian::big_uint16_buf_t freelist;      // Index of the freelist head entry
  boost::endian::big_uint16_buf_t allocated;     // How many entries are allocated
  boost::endian::big_uint16_buf_t start_offset;  // Where the array start in the bock
  boost::endian::big_uint16_buf_t total_bytse;   // The size of the array in bytes
};
static_assert(sizeof(TreeSubBlockAllocatorHeader) == 0x8, "Incorrect sizeof TreeSubBlockAllocatorHeader");

struct NodesHeapHeader {
  boost::endian::big_uint16_buf_t freelist_head_offset;
  boost::endian::big_uint16_buf_t allocated_blocks;
  boost::endian::big_uint16_buf_t start_offset;
  boost::endian::big_uint16_buf_t total_bytes;
};

struct NodesHeapFreelistEntry {
  boost::endian::big_uint32_buf_t init_zero;  // zero, never used
  boost::endian::big_uint32_buf_t next;       // index in the heap entries list
  boost::endian::big_uint16_buf_t count;      // freed entries count
  boost::endian::big_uint16_buf_t padding1;
  boost::endian::big_uint32_buf_t padding2;
};

// The nodes represent ranges. There may between 1 and 6 sub-nodes.
// The key represent the split point. So there can be between 0-5 keys (they end with zero keys)
// so x < keys[0] go to node[0], keys[0] <= x < keys[1] go to node[1] and etc..
struct RTreeNode_details {
  boost::endian::big_uint32_buf_t keys[5];
  boost::endian::big_uint16_buf_t values[6];
};
static_assert(sizeof(RTreeNode_details) == 0x20);

struct RTreeLeaf_details {
  boost::endian::big_uint32_buf_t keys[4];
  boost::endian::big_uint32_buf_t values[4];
};
static_assert(sizeof(RTreeLeaf_details) == 0x20);

struct FTreeLeaf_details {
  boost::endian::big_uint32_buf_t keys[7];
  boost::endian::big_uint32_buf_t values;
};
static_assert(sizeof(FTreeLeaf_details) == 0x20);
struct PTreeHeader {
  boost::endian::big_uint16_buf_t tree_depth;
  boost::endian::big_uint16_buf_t type;
  boost::endian::big_uint16_buf_t root_offset;
  boost::endian::big_uint16_buf_t items_count;
};
static_assert(sizeof(PTreeHeader) == 0x8);

struct EPTreeBlockFooter {
  PTreeHeader current_tree;
  boost::endian::big_uint32_buf_t block_number;
  boost::endian::big_uint8_buf_t depth;
  boost::endian::big_uint8_buf_t padding[0xb];
  NodesHeapHeader heap_hader;
};
static_assert(sizeof(EPTreeBlockFooter) == 0x20);

struct FTreesBlockFooter {
  PTreeHeader trees[7];  // tree per each size of block
  NodesHeapHeader heap_hader;
};
static_assert(sizeof(FTreesBlockFooter) == 0x40);
