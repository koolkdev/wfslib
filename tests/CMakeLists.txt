cmake_minimum_required(VERSION 3.18)
project(wfslib_tests)

find_package(Catch2 CONFIG REQUIRED)

enable_testing()

set(WFSLIB_BEHAVIOR_TEST_SOURCES
  block_tests.cpp
  eptree_tests.cpp
  file_layout_accessor_tests.cpp
  file_layout_tests.cpp
  file_resize_tests.cpp
  free_blocks_allocator_tests.cpp
  free_blocks_tree_bucket_tests.cpp
  free_blocks_tree_tests.cpp
  ftree_tests.cpp
  ftrees_tests.cpp
  rtree_tests.cpp
  sub_block_allocator_tests.cpp
  tree_nodes_allocator_tests.cpp
)

# Requirements: P2374R4 and P2164R9.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(APPEND WFSLIB_BEHAVIOR_TEST_SOURCES
    directory_map_tests.cpp
    directory_tree_tests.cpp
  )
endif()

set(WFSLIB_TEST_SUPPORT_SOURCES
  utils/test_area.cpp
  utils/test_block.cpp
  utils/test_blocks_device.cpp
  utils/test_free_blocks_allocator.cpp
)

add_executable(${PROJECT_NAME}
  ${WFSLIB_BEHAVIOR_TEST_SOURCES}
  ${WFSLIB_TEST_SUPPORT_SOURCES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
  ${WFSLIB_BEHAVIOR_TEST_SOURCES}
  ${WFSLIB_TEST_SUPPORT_SOURCES}
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
