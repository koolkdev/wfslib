# Commit summary for current branch

- Replace uses of pipe syntax with `std::ranges::to` adaptor closure in `directory_tree`, iterators, quota handling, and tests with direct `std::ranges::to(...)` calls to avoid Clang 21/libstdc++ instantiation bugs.
- Ensure transformed ranges are materialized via explicit conversions to strings/vectors rather than piping, preserving behavior while improving compiler compatibility.
