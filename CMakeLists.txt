cmake_minimum_required(VERSION 3.6)

project(DRME C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GCC_COVERAGE_COMPILE_FLAGS
    "-Wall -O0 -g -D_FILE_OFFSET_BITS=64"
)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")

include_directories(
    "/usr/include/libdrm",
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(drme_legacy 
    legacy.cpp    
)
target_link_libraries(drme_legacy drm)

add_executable(drme_atomic_gbm
    gbm_atomic.cpp
    gbm_allocator.cpp
)
target_link_libraries(drme_atomic_gbm drm gbm EGL)

add_executable(drme_atomic_shm
    shm_atomic.cpp
    shm_allocator.cpp
)
target_link_libraries(drme_atomic_shm drm)

add_executable(drme_mesa_gbm_demo
    mesa_gbm_demo.cpp
)
target_link_libraries(drme_mesa_gbm_demo drm gbm EGL GL GLU glut)
