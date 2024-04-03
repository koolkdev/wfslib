cmake_minimum_required(VERSION 3.18)
project(wfslib_tests)

find_package(Catch2 CONFIG REQUIRED)

enable_testing()

add_executable(${PROJECT_NAME}
  eptree_tests.cpp
  free_blocks_tree_bucket_tests.cpp
  free_blocks_tree_tests.cpp
  ftree_tests.cpp
  ftrees_tests.cpp
  test_blocks_device.cpp
  test_free_blocks_allocator.cpp
  test_metadata_block.cpp
  rtree_tests.cpp
  tree_nodes_allocator_tests.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
)

get_property(wfs_include_dirs TARGET wfslib PROPERTY INCLUDE_DIRECTORIES)

target_include_directories(${PROJECT_NAME} PRIVATE ${wfs_include_dirs})

target_link_libraries(${PROJECT_NAME}
  PRIVATE
  Boost::boost
    Catch2::Catch2WithMain
    wfslib
)

include(CTest)
include(Catch)
catch_discover_tests(${PROJECT_NAME})
