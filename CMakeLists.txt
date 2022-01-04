cmake_minimum_required(VERSION 3.19)

project(wfslib)

option(BUILD_STATIC_MSVC "Build static with MSVC" OFF)

add_library(wfslib
    src/Area.cpp
    src/Block.cpp
    src/DataBlock.cpp
    src/DeviceEncryption.cpp
    src/Directory.cpp
    src/File.cpp
    src/FileDevice.cpp
    src/KeyFile.cpp
    src/MetadataBlock.cpp
    src/Structs.cpp
    src/SubBlockAllocator.cpp
    src/Wfs.cpp
    src/WfsItem.cpp
)

if (BUILD_STATIC_MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_compile_features(wfslib PRIVATE cxx_std_11)
#target_compile_options(${PROJECT_NAME} PRIVATE
#  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
#  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
#)

target_include_directories(wfslib
    PUBLIC 
        $<INSTALL_INTERFACE:include>    
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include/wfslib
)

find_package(Boost 1.64.0 COMPONENTS filesystem REQUIRED)
find_package(cryptopp REQUIRED CONFIG)
target_link_libraries(wfslib
    PUBLIC
        Boost::boost Boost::filesystem
    PRIVATE
        cryptopp-static
)
