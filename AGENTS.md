# Repository Guidelines

## Project Structure & Module Organization
The public API lives in `include/wfslib`, with headers mirroring the snake_case filenames in `src/` for implementations. Core data-structure code (trees, allocators, devices) resides directly under `src/`, while shared testing scaffolding is under `tests/utils`. CMake configuration is rooted at `CMakeLists.txt`; tooling and third-party dependencies are managed via `vcpkg/` and the presets defined in `CMakePresets.json`.

## Build, Test, and Development Commands
- Configure with `cmake --preset default` (or `static`, `windows-static`) to generate a Ninja multi-config build in `build/<preset>/`.
- Build with `cmake --build --preset debug` or `--preset release` depending on the desired configuration.
- Enable tests by configuring `cmake --preset tests`, then build via `cmake --build --preset debug-tests`.
- Run the Catch2 suite using `ctest --preset run-debug-tests`; pass `--verbose` when diagnosing failures.

## Coding Style & Naming Conventions
Format C++ with `clang-format -i` using the Chromium-derived rules in `.clang-format` (120-column limit, 2-space indents). Keep headers and sources paired by filename (`directory_map.h` / `.cpp`), use PascalCase for public classes and factories, and prefer lower_snake_case for free functions, locals, and file names. Preserve existing namespace usage and wrap platform-specific code in explicit helpers rather than preprocessor conditionals in-line.

## Architecture Overview
`WfsDevice` orchestrates volume access via `BlocksDevice`, which caches blocks and applies AES-CBC through `DeviceEncryption`. Areas partition disk space: `QuotaArea` drives quotas and `FreeBlocksAllocator` trees, while `TransactionsArea` covers journal space. `DirectoryMap` balances B-tree metadata blocks, allocates entry payloads, and requests extra blocks from the quota; suites in `tests/` emulate large directories on in-memory devices to stress these paths.

## Testing Guidelines
Tests rely on Catch2 (`<catch2/catch_test_macros.hpp>`). Place new suites in `tests/` with a `*_tests.cpp` suffix and register scenarios via `TEST_CASE`. Share fixtures or mock devices through `tests/utils`. After adding code, rebuild with the matching `*-tests` preset and run `ctest --preset run-debug-tests`; aim to cover new branches and edge cases, especially around tree balancing and block allocation logic.

## Commit & Pull Request Guidelines
Follow the existing history pattern: start subjects with the touched module (`DirectoryMap:`, `Build workflow:`) followed by a concise action, and append the related issue in parentheses when applicable (e.g. `(#96)`). PRs should summarize behavior changes, link issues or discussions, note any vcpkg updates, and include test evidence (`ctest --preset run-debug-tests`). Provide repro steps for bug fixes and highlight impacts on downstream tools consuming the library.
