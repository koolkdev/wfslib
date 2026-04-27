# WFS Directory And File Creation Plan

This plan covers adding real create-file and create-directory support to wfslib.
It is intentionally staged so the public creation API is built on top of
well-defined directory-map insertion, metadata construction, and rollback
behavior.

## Goal

Support creating regular files and directories through the library API:

- Create a file entry in an existing directory.
- Create a directory entry and initialize its directory tree block.
- Create by absolute path from `WfsDevice`.
- Preserve WFS name casing while using case-insensitive directory keys.
- Return live `File` / `Directory` objects wired to the same `DirectoryMap`
  cache used by lookup and resize.
- Keep failure handling focused on real recoverable conditions, especially
  duplicate names and no space.

Out of scope for the first implementation:

- Links.
- Creating quota-backed directories.
- Recursive delete or rename.
- Transaction-area integration.
- Rich permission/timestamp policy beyond simple caller-supplied/default
  metadata fields.

## Current State

The low-level storage pieces mostly exist:

- `DirectoryMap::insert()` can insert a prepared `EntryMetadata` blob.
- `DirectoryMap::erase()` removes entries and frees their metadata allocation.
- `DirectoryMap::replace_metadata()` already relocates metadata and refreshes
  live entry handles.
- `QuotaArea::AllocMetadataBlock()` and `DeleteBlocks()` provide the block
  allocation needed for new directories.
- `File::file_device::write()` can grow a file through `File::Resize()`, so an
  empty created file can be populated through the existing stream path.
- `QuotaArea` caches `DirectoryMap` instances by directory block number, which
  is the right ownership shape for newly created directories.

The public surface is still read-only:

- `Directory` exposes `GetEntry`, `GetFile`, `GetDirectory`, iteration, and
  lookup, but no create methods.
- `WfsDevice` exposes path-based lookup, but no path-based create methods.
- Tests create entries by directly calling `DirectoryMap::insert()` with hand
  built metadata.

## Gaps

### Root Directory Initialization

`WfsDevice::Create()` initializes the area and free-block allocator, but
`QuotaArea::Init()` still has a TODO for root and shadow directories. Tests work
around this by manually loading block 3 and calling `DirectoryMap::Init()`.

Creation APIs should not depend on that test-only setup. A freshly created WFS
device must have initialized root, shadow 1, and shadow 2 directory blocks.

### DirectoryMap Insert Result And Error Semantics

`DirectoryMap::insert()` currently returns `bool`.

That is not enough for public creation:

- Duplicate entry and allocation failure are different outcomes.
- The caller needs the inserted metadata ref to return a live entry without
  doing avoidable extra tree work.
- `split_tree()` currently throws through `throw_if_error()` on metadata-block
  allocation failure and has a TODO for no-space behavior.
- Insertion after allocating a directory block needs rollback if entry insertion
  fails.

### Name Normalization And Case Bitmap

Lookups lowercase in `Directory::find()`, while case restoration happens in
`Entry::name()` through `EntryMetadata::GetCaseSensitiveName()`.

Creation needs one shared name helper that:

- Validates a single path component.
- Produces the lower-case directory key.
- Sets `filename_length`.
- Encodes `case_bitmap` from the caller-provided spelling.
- Can be reused by both file and directory creation.

There should also be tests around mixed-case names. The current
`GetCaseSensitiveName()` path deserves attention because it should read the
rounded-up case-bitmap byte count, not rely on ad hoc byte ranges.

### Metadata Construction

Tests hand-fill `EntryMetadata`. Public creation needs a production helper that
builds a correctly sized metadata allocation for each new entry kind.

For regular files, the helper can use `FileLayout::Calculate(0, initial_size,
filename_length, block_size_log2, FileLayoutCategory::Inline)`. The first create
API should create empty files and let existing stream writes grow them. Creating
with initial data can be layered on later.

For directories, metadata is not file-layout driven. It needs:

- `DIRECTORY` flag.
- `directory_block_number`.
- zero file size and size-on-disk fields.
- metadata log2 size large enough for the base metadata and case bitmap.

### Directory Creation Rollback

Creating a directory has two allocations:

1. Allocate and initialize the child directory metadata block.
2. Insert the parent directory entry that points at that block.

If step 2 fails because the parent directory has no room and cannot split, the
new child block must be released and detached. This is the main recoverable
failure path to handle.

### Public API Shape

The library currently mixes pointer-returning lookup on `WfsDevice` with
`std::expected` on lower-level APIs. Creation should prefer `std::expected`
because callers need to distinguish duplicate, parent-not-found, not-directory,
file-too-large, and no-space outcomes.

Recommended API shape:

```cpp
class Directory {
 public:
  std::expected<std::shared_ptr<File>, WfsError> CreateFile(std::string_view name);
  std::expected<std::shared_ptr<Directory>, WfsError> CreateDirectory(std::string_view name);
};

class WfsDevice {
 public:
  std::expected<std::shared_ptr<File>, WfsError> CreateFile(std::string_view path);
  std::expected<std::shared_ptr<Directory>, WfsError> CreateDirectory(std::string_view path);
};
```

`WfsDevice::CreateDirectory()` should create only one final component. Recursive
`CreateDirectories()` can be added later if needed.

## Proposed Implementation Stages

### PR 1: Initialize Directories On Format

Initialize reserved root and shadow directory blocks during `QuotaArea::Init()`:

- Load block 3, 4, and 5 with `new_block=true`.
- Run `DirectoryMap::Init()` on each.
- Add tests that a fresh `WfsDevice::Create()` can immediately resolve `/` and
  iterate an empty root directory.

This is a prerequisite for public creation on newly formatted devices.

### PR 2: Add Entry Name And Metadata Builders

Add a small helper for directory-entry names and metadata construction.

Suggested responsibilities:

- Normalize a single entry name to the directory key.
- Build the case bitmap.
- Reject empty names and names containing `/`.
- Reject names longer than `uint8_t` can store.
- Compute the base metadata allocation log2 size.
- Build empty-file metadata using `FileLayout`.
- Build directory metadata for a known directory block number.

This should be tested independently from `DirectoryMap`.

### PR 3: Make DirectoryMap Insertion Usable By Creation

Add a result-bearing insertion API while preserving existing tests during the
transition.

Suggested shape:

```cpp
std::expected<Block::DataRef<EntryMetadata>, WfsError>
DirectoryMap::insert_metadata(std::string_view key, const EntryMetadata* metadata);
```

Behavior:

- Return a real duplicate-entry error for existing keys.
- Return `kNoSpace` when metadata allocation or tree split cannot allocate.
- Return the new metadata ref on success.
- Refresh live metadata handles if a leaf split moves existing entries.
- Keep the old `bool insert(...)` as a thin compatibility wrapper until tests
  are migrated.

This PR should also make `split_tree()` stop throwing for normal no-space
allocation failure. It can still assert on impossible corruption states.

### PR 4: Add `Directory::CreateFile`

Implement single-component file creation on an existing `Directory`:

- Normalize the requested name.
- Build empty-file metadata.
- Insert through the new `DirectoryMap` API.
- Return the created `File` by loading the inserted entry through the
  directory map/cache.

Tests:

- Create file in root, look it up by exact and different case.
- Repeated path lookup returns the same live instance while it is still held.
- Duplicate create fails and leaves the original file intact.
- Write to a created empty file and read it back.
- Directory tree split during file creation keeps existing live entries valid.
- No-space during insertion leaves no partially visible entry.

### PR 5: Add `Directory::CreateDirectory`

Implement single-component directory creation:

- Allocate a metadata block for the new directory.
- Initialize that block as a directory map.
- Build parent entry metadata pointing at the new block.
- Insert the parent entry.
- On insert failure, delete the new block and detach it.
- Return the created `Directory` through the directory map/cache.

Tests:

- Create a directory and then create a file inside it.
- Repeated lookup returns the same live directory instance.
- Duplicate directory create fails.
- Creating a directory where a file already exists fails.
- No-space after child block allocation rolls the child block back.
- Tree split during directory create refreshes live metadata handles.

### PR 6: Add `WfsDevice` Path-Based Creation

Layer absolute path helpers on top of `Directory` creation:

- Resolve the parent path through existing `GetDirectory()`.
- Return `kEntryNotFound` when the parent does not exist.
- Return `kNotDirectory` when an intermediate component is a file.
- Reject `/` as a creatable file or directory path.
- Create only the final component.

Tests:

- `CreateFile("/a")`.
- `CreateDirectory("/dir")`.
- `CreateFile("/dir/file")`.
- Parent missing.
- Parent is a file.
- Duplicate path.
- Mixed-case path lookup after creation.

## Suggested Error Additions

The current `WfsError` set has no duplicate-entry or invalid-name error. Those
are real domain errors for creation and should be added instead of overloading
`kEntryNotFound` or `kDirectoryCorrupted`.

Suggested additions:

- `kEntryAlreadyExists`
- `kInvalidEntryName`

If the preference is to avoid growing `WfsError` in the first PR, the lower
level can temporarily keep duplicate reporting as `bool`, but the public create
API will be weaker until these errors exist.

## Notes For Later

- `CreateFile(name, initial_data)` can be implemented after empty-file creation
  by creating the file, writing through `File::file_device`, and erasing the
  entry if the initial write fails before commit semantics are defined.
- Quota directory creation is a separate feature because it needs
  `AllocAreaBlocks()`, `QuotaArea::Create()`, parent metadata flags, and
  rollback across area creation.
- Delete and rename should reuse the same name helper and cache invalidation
  rules, but they should not block basic creation.
