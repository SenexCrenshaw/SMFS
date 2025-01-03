cmake_minimum_required(VERSION 3.20)
project(smfs_fuse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wpedantic")

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(CURL REQUIRED libcurl)

# Source Files
set(SOURCE_FILES
    src/main.cpp
    src/logger.cpp
    src/api_client.cpp    
    src/fuse_manager.cpp
    src/async_curl_client.cpp
    src/stream_manager.cpp
    src/websocket_client.cpp
    src/directory_operations.cpp
    src/file_operations.cpp
    src/lookup_operations.cpp
    src/util_operations.cpp
)

# Include Files
set(INCLUDE_FILES
    include/i_streaming_client.hpp
    include/async_curl_client.hpp
    include/logger.hpp
    include/api_client.hpp
    include/smfs_state.hpp
    include/fuse_operations.hpp
    include/stream_manager.hpp
    include/websocket_client.hpp
    include/fuse_manager.hpp
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

# Installation Rules
install(TARGETS smfs DESTINATION bin)

# Install Headers
install(FILES ${INCLUDE_FILES} DESTINATION include/smfs)

# Install Required Directories or Configuration Files (Optional)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/config DESTINATION etc/smfs OPTIONAL)
