cmake_minimum_required(VERSION 3.4.3)
project(FindReplace)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

if(NOT COMMAND testBuilder_build)
    add_subdirectory(testBuilder)
    testBuilder_set_current_working_directory_to_default_binary_directory()
endif()

set(PLATFORM Linux)

add_subdirectory(cppfs)
add_subdirectory(mmaptwo-plus)
add_subdirectory(TempFile)

include_directories(mmaptwo-plus)
include_directories(include)

testBuilder_add_source(ifstream_iterator src/ifstream_iterator.cpp)
testBuilder_add_include(ifstream_iterator include)
testBuilder_build_shared_library(ifstream_iterator)

testBuilder_add_source(mmap src/mmap.cpp)
testBuilder_add_source(mmap src/mmap_iterator.cpp)
testBuilder_add_include(mmap include)
testBuilder_add_library(mmap mmaptwo_plus)
testBuilder_build_shared_library(mmap)

testBuilder_add_source(FindReplace src/main.cpp)
testBuilder_add_library(FindReplace mmap)
testBuilder_add_library(FindReplace cppfs)
testBuilder_add_library(FindReplace tmpfile)
testBuilder_add_library(FindReplace ifstream_iterator)
testBuilder_build(FindReplace EXECUTABLES)
