cmake_minimum_required(VERSION 3.6)

project(DRME C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GCC_COVERAGE_COMPILE_FLAGS
    "-Wall -O0 -g -D_FILE_OFFSET_BITS=64"
)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")

include_directories("/usr/include/libdrm")

add_executable(drme_legacy legacy.cpp)
target_link_libraries(drme_legacy drm)
