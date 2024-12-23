cmake_minimum_required(VERSION 3.10)
project(smfs_fuse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wpedantic")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(CURL REQUIRED libcurl)

set(SOURCE_FILES
    src/main.cpp
    src/logger.cpp
    src/websocket_client.hpp
    include/sgfs.hpp
    include/stream_context.hpp
    include/api_client.hpp
    src/api_client.cpp
    src/smfs_state.hpp    
    include/fuse_operations.hpp
    src/fuse_operations.cpp
    include/ring_buffer.hpp
)

# Executable
add_executable(smfs ${SOURCE_FILES})

target_include_directories(smfs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(smfs
    ${FUSE3_LIBRARIES}
    ${CURL_LIBRARIES}
    pthread
)
