/*
 * Copyright (C) 2017 koolkdev
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <span>
#include <string>
#include <vector>

#include "utils.h"

static constexpr uint32_t WFS_VERSION = 0x01010800;

// sizeof 0x18
struct MetadataBlockHeader {
  // Flags:
  // 0x00400000 - Area header
  // 0x00800000 - Root area
  // 0x20000000 - leaf directory tree
  // 0x40000000 - ?
  // 0x80000000 - Directory?
  enum Flags : uint32_t {
    AREA = 0x00400000,
    ROOT_AREA = 0x00800000,
    EXTERNAL_DIRECTORY_TREE = 0x20000000,
    UNKNOWN = 0x40000000,
    DIRECTORY = 0x80000000,
  };
  uint32_be_t block_flags;  // 20 least bits ignored
  uint8_be_t hash[20];
};
static_assert(sizeof(MetadataBlockHeader) == 0x18, "Incorrect sizeof MetadataBlockHeader");

// sizeof 0x18
struct DataBlockMetadata {
  uint32_be_t block_number;
  uint8_be_t hash[20];
};
static_assert(sizeof(DataBlockMetadata) == 0x18, "Incorrect sizeof DataBlockMetadata");

// sizeof 0xa4
struct DataBlocksClusterMetadata {
  uint32_be_t block_number;
  uint8_be_t hash[8][20];
};
static_assert(sizeof(DataBlocksClusterMetadata) == 0xa4, "Incorrect sizeof DataBlocksClusterMetadata");

// sizeof 0xc
struct Permissions {
  uint32_be_t owner;
  uint32_be_t group;
  uint32_be_t mode;
};
static_assert(sizeof(Permissions) == 0xc, "Incorrect sizeof Permissions");

// sizeof 0x2c
struct Attributes {
  enum Flags : uint32_t {
    UNENCRYPTED_FILE = 0x2000000,
    LINK = 0x4000000,
    AREA_SIZE_BASIC = 0x10000000,
    AREA_SIZE_REGULAR = 0x20000000,
    QUOTA = 0x40000000,
    DIRECTORY = 0x80000000,
  };
  uint32_be_t flags;
  uint32_be_t size_on_disk;
  uint32_be_t ctime;
  uint32_be_t mtime;
  uint32_be_t unknown;  // TODO: ????
  union {
    uint32_be_t size;          // for file
    uint32_be_t blocks_count;  // for quota
  };
  uint32_be_t directory_block_number;  // in case of directory
  Permissions permissions;
  uint8_be_t entry_log2_size;  // log2 size of the whole entry, including this attributes
  uint8_be_t size_category;    // 0-4, see File.c
  uint8_be_t filename_length;
  uint8_be_t case_bitmap;  // This byte in the struct also behave as padding, it isn't really a
                           // byte, it is a bitmap of filename_length

  bool IsDirectory() const { return !!(flags.value() & Flags::DIRECTORY); }
  bool IsFile() const { return !IsDirectory(); }
  bool IsLink() const { return !!(flags.value() & Flags::LINK); }
  bool IsQuota() const { return !!(flags.value() & Flags::QUOTA); }

  size_t DataOffset() const;

  std::string GetCaseSensitiveName(const std::string& name) const;
};
static_assert(sizeof(Attributes) == 0x2C, "Incorrect sizeof Attributes");

enum class DeviceType : uint16_t {
  MLC = 0x136a,
  USB = 0x16a2,
};

// sizeof 0x48
struct WfsHeader {
  uint32_be_t iv;           // most 2 bits are device type. 1-mlc/3-usb
  uint32_be_t version;      // should be 0x01010800
  uint16_be_t device_type;  // usb - 0x16a2. mlc - 0x136a?
  uint16_be_t _pad;
  Attributes root_area_attributes;
  uint32_be_t transactions_area_block_number;  // must be 6 or 12 (6*2, when regular block size used)
  uint32_be_t transactions_area_blocks_count;
  uint32_be_t unknown[2];  // not used??
};
static_assert(sizeof(WfsHeader) == 0x48, "Incorrect sizeof WfsHeader");

// sizof 0x8
struct WfsAreaFragmentInfo {
  uint32_be_t block_number;
  uint32_be_t blocks_count;
};
static_assert(sizeof(WfsAreaFragmentInfo) == 0x8, "Incorrect sizeof WfsAreaFragmentInfo");

// sizeof 0x60
struct WfsAreaHeader {
  enum class AreaType : uint8_t {
    TransactionsArea = 0,
    QuotaArea = 1,
  };
  uint32_be_t iv;  // used for blocks  encryption
  uint32_be_t blocks_count;
  uint32_be_t root_directory_block_number;  // is 3
  // Those two directories are not used. It was supposed to be some kind of refcount mechansism. not really sure.
  // But nothing uses it in the WiiU
  uint32_be_t shadow_directory_block_number_1;  // is 4
  uint32_be_t shadow_directory_block_number_2;  // is 5
  uint8_be_t depth;                             // how many total parents area this area has
  uint8_be_t block_size_log2;                   // 12/13
  uint8_be_t large_block_size_log2;             // large block - 8 blocks. so 15/16
  uint8_be_t large_block_cluster_size_log2;     // large blocks cluster - 8 large blocks. so 18/19
  uint8_be_t area_type;                         // 0 - transactions area, 1 - quota area
  uint8_be_t maybe_always_zero;                 // init to zero. doesn't seem to be changed by anything
  uint16_be_t remainder_blocks_count;  // in case of quota, how many spare blocks this area allocated beyond the quota.
                                       // (parent area blocks, so in parent area block size)
  WfsAreaFragmentInfo first_fragments[8];  // On which blocks this area is spread on (if there aren't enough free
                                           // sequentials blocks it will framgnet the area). parent area blocks, so in
                                           // parent area block size. This list contains only the first 8 fragments
  uint32_be_t fragments_log2_block_size;   // block size of parent area
};
static_assert(sizeof(WfsAreaHeader) == 0x60, "Incorrect sizeof WfsAreaHeader");

// sizof 0x8
struct WfsAreaFragmentsInfo {
  uint16_be_t max_fragments_count;        // 480
  uint16_be_t fragments_log2_block_size;  // block size of parent area. In root area it is the minimum block size (12)
  uint32_be_t fragments_count;
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
  uint8_be_t unknown[0x3B4];
};
static_assert(sizeof(WfsTransactionsArea) == 0x60 + 0x3B4, "Incorrect sizeof WfsTransactionsArea");

struct SubBlockAllocatorFreeListEntry {
  static const uint16_t FREE_MARK_CONST = 0xFEDC;

  uint16_be_t free_mark;
  uint16_be_t next;
  uint16_be_t prev;
  uint16_be_t log2_block_size;
};
static_assert(sizeof(SubBlockAllocatorFreeListEntry) == 0x8, "Incorrect sizeof SubBlockAllocatorFreeListEntry");

struct SubBlockAllocatorFreeList {
  uint16_be_t free_blocks_count;
  uint16_be_t head;  // head free block
};
static_assert(sizeof(SubBlockAllocatorFreeList) == 0x4, "Incorrect sizeof SubBlockAllocatorFreeList");

struct SubBlockAllocatorStruct {
  SubBlockAllocatorFreeList free_list[8];  // for sizes 1<<3-10
};
static_assert(sizeof(SubBlockAllocatorStruct) == 0x20, "Incorrect sizeof SubBlockAllocatorStruct");

struct DirectoryTreeHeader {
  uint16_be_t root;
  uint16_be_t records_count;
};
static_assert(sizeof(DirectoryTreeHeader) == 0x4, "Incorrect sizeofDirectoryTreeHeader");

struct DirectoryTreeNode {
  uint8_be_t prefix_length;
  uint8_be_t choices_count;

 public:
  std::string prefix() const { return {prefix_view().begin(), prefix_view().end()}; }
  std::string_view prefix_view() const { return {reinterpret_cast<const char*>(this + 1), prefix_length.value()}; }
  std::span<const std::byte> choices() const {
    return {reinterpret_cast<const std::byte*>(this + 1) + prefix_length.value(), choices_count.value()};
  }
};
static_assert(sizeof(DirectoryTreeNode) == 0x2, "Incorrect sizeof DirectoryTreeNode");

struct ExternalDirectoryTreeNode : DirectoryTreeNode {
 private:
  size_t size() const;

 public:
  uint16_be_t get_item(size_t index) const {
    return reinterpret_cast<const uint16_be_t*>(reinterpret_cast<const std::byte*>(this) +
                                                size())[-static_cast<int>(index) - 1];
  }
};

struct InternalDirectoryTreeNode : DirectoryTreeNode {
 private:
  size_t size() const;

 public:
  uint16_be_t get_item(size_t index) const {
    return reinterpret_cast<const uint16_be_t*>(reinterpret_cast<const std::byte*>(this) + size() -
                                                (choices()[0] == std::byte{0} ? 2 : 0))[-static_cast<int>(index) - 1];
  }
  uint32_be_t get_next_allocator_block_number() const {
    return reinterpret_cast<const uint32_be_t*>(reinterpret_cast<const std::byte*>(this) + size())[-1];
  }
};

struct FreeBlocksAllocatorHeader {
  uint32_be_t free_blocks_count;
  uint32_be_t always_one;  // initialized to 1
  // When createa a new area, a fixed amount of blocks are allocated for metadata blocks for quick allocation. When
  // allocating metadata blocks, it will advance |free_metadata_block| and will decrease |free_metadata_blocks_count|.
  // When no more availabe blocks are avaialbe, it will allocate them regulary.
  // TODO: This is not metadata specific blocks, so rename this, this is just quick alloc cache
  uint32_be_t free_blocks_cache;
  uint32_be_t free_blocks_cache_count;
};
static_assert(sizeof(FreeBlocksAllocatorHeader) == 0x10, "Incorrect sizeof FreeBLocksAllocatorHeader");

// Header at the end of the block. Contains an array of entries of a constant size
struct HeapHeader {
  uint16_be_t freelist_head;      // Index of the freelist head entry
  uint16_be_t allocated_entries;  // How many entries are allocated
  uint16_be_t start_offset;       // Where the array start in the bock
  uint16_be_t total_bytes;        // The size of the array in bytes
};
static_assert(sizeof(HeapHeader) == 0x8, "Incorrect sizeof HeapHeader");

struct HeapFreelistEntry {
  uint32_be_t init_zero;  // zero, never used
  uint32_be_t next;       // index in the heap entries list
  uint16_be_t count;      // freed entries count
};
static_assert(sizeof(HeapFreelistEntry) == 0xA, "Incorrect sizeof HeapFreelistEntry");

// The nodes represent ranges. There may between 1 and 6 sub-nodes.
// The key represent the split point. So there can be between 0-5 keys (they end with zero keys)
// so x < keys[0] go to node[0], keys[0] <= x < keys[1] go to node[1] and etc..
struct PTreeNode_details {
  uint32_be_t keys[5];
  uint16_be_t values[6];
};
static_assert(sizeof(PTreeNode_details) == 0x20);

struct RTreeLeaf_details {
  uint32_be_t keys[4];
  uint32_be_t values[4];
};
static_assert(sizeof(RTreeLeaf_details) == 0x20);

struct FTreeLeaf_details {
  uint32_be_t keys[7];
  uint32_be_t values;
};
static_assert(sizeof(FTreeLeaf_details) == 0x20);
struct PTreeHeader {
  uint16_be_t tree_depth;
  uint16_be_t type;
  uint16_be_t root_offset;
  uint16_be_t items_count;
};
static_assert(sizeof(PTreeHeader) == 0x8);

struct EPTreeFooter {
  PTreeHeader current_tree;
  uint32_be_t block_number;
  uint8_be_t depth;
  uint8_be_t padding[0xb];
};
static_assert(sizeof(EPTreeFooter) == 0x18);

struct FTreesFooter {
  PTreeHeader trees[7];  // tree per each size of block
};
static_assert(sizeof(FTreesFooter) == 0x38);

struct FTreesBlockHeader {
  uint8_be_t padding[8];
};
static_assert(sizeof(FTreesBlockHeader) == 8);
