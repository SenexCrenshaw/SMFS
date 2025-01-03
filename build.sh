#!/bin/bash

set -e # Exit on any error

# Define build directories
BUILD_DIR="build"
INSTALL_DIR="$BUILD_DIR/install"

echo "Cleaning up previous builds..."
rm -rf "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

echo "Configuring the build system..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

echo "Building the project..."
cmake --build "$BUILD_DIR" --target all -- -j$(nproc)

echo "Installing the project into the staging area..."
cmake --install "$BUILD_DIR" --prefix "$INSTALL_DIR"

echo "Build completed. Artifacts available in: $INSTALL_DIR"
