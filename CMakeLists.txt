cmake_minimum_required(VERSION 3.18)

project(wfslib)

# Check compiler verions
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0)
        message(FATAL_ERROR "GCC version must be at least 11.0!")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0)
        message(FATAL_ERROR "Clang version must be at least 16.0!")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.34)
        message(FATAL_ERROR "Visual Studio must be at least VS2022 17.4!")
    endif()
else()
    message(WARNING "You are using an unsupported compiler! Please use MSVC/GCC/Clang")
endif()

option(BUILD_STATIC "Build static" OFF)

add_library(${PROJECT_NAME}
    src/area.cpp
    src/block.cpp
    src/blocks_device.cpp
    src/data_block.cpp
    src/device_encryption.cpp
    src/directory.cpp
    src/directory_items_iterator.cpp
    src/eptree.cpp
    src/errors.cpp
    src/file.cpp
    src/file_device.cpp
    src/free_blocks_allocator.cpp
    src/free_blocks_tree_bucket.cpp
    src/ftrees.cpp
    src/key_file.cpp
    src/metadata_block.cpp
    src/ptree.cpp
    src/recovery.cpp
    src/structs.cpp
    src/sub_block_allocator.cpp
    src/wfs.cpp
    src/wfs_item.cpp
)

if(BUILD_STATIC AND MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
)

target_include_directories(${PROJECT_NAME}
    PUBLIC 
        $<INSTALL_INTERFACE:include>    
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include/wfslib
)

find_package(Boost 1.64.0 REQUIRED)
find_package(cryptopp REQUIRED)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Boost::boost
    PRIVATE
        cryptopp::cryptopp
)

if(CONFIG STREQUAL "Release" AND NOT MSVC)
    target_link_libraries(${PROJECT_NAME} PUBLIC -s)
endif()

if(BUILD_STATIC AND NOT MSVC AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PUBLIC -static-libstdc++ -static-libgcc)
endif()
