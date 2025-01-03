#!/bin/bash

set -e # Exit on any error

# Define paths and metadata
BUILD_DIR="build"
INSTALL_DIR="$BUILD_DIR/install"
PACKAGE_DIR="$BUILD_DIR/package-deb"
VERSION="1.0.0"
ARCH="amd64" # Change to your architecture
PACKAGE_NAME="smfs"

echo "Preparing the Debian package directory..."
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR/DEBIAN"
mkdir -p "$PACKAGE_DIR/usr/local"

echo "Copying files to the Debian package directory..."
cp -r "$INSTALL_DIR/"* "$PACKAGE_DIR/usr/local"

echo "Creating control file..."
cat > "$PACKAGE_DIR/DEBIAN/control" <<EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: Senex Crenshaw <senexcrenshaw@gmail.com>
Description: Stream Master File System
 A brief description of SMFS.
EOF

echo "Building the .deb package..."
dpkg-deb --build "$PACKAGE_DIR"

echo "Moving the package to the output directory..."
mv "$PACKAGE_DIR.deb" "$BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"

echo "Debian package created: $BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
