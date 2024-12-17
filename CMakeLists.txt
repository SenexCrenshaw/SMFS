cmake_minimum_required(VERSION 3.10)
project(SMFS VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Use vcpkg toolchain
set(CMAKE_TOOLCHAIN_FILE "/opt/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
set(CMAKE_PREFIX_PATH "/opt/vcpkg/installed/x64-linux")

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3)
find_package(nlohmann_json REQUIRED)

# Source files
set(SOURCE_FILES
    src/main.cpp
    src/virtualfs
    src/api_client.cpp
    src/smfs_state.hpp
    src/websocket_client.hpp
    src/fuse_operations.cpp
    src/logger.cpp
)

# Executable
add_executable(smfs ${SOURCE_FILES})

# uWebSockets includes and library paths
target_include_directories(smfs PRIVATE /opt/vcpkg/installed/x64-linux/include ${FUSE_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
target_link_directories(smfs PRIVATE /opt/vcpkg/installed/x64-linux/lib)

# Link libraries
target_link_libraries(smfs PRIVATE
    ${FUSE_LIBRARIES}
    curl
    pthread
    uSockets
    z
)
