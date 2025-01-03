#!/bin/bash

set -e # Exit on any error

# Define paths and metadata
BUILD_DIR="build"
INSTALL_DIR="$BUILD_DIR/install"
PACKAGE_DIR="$BUILD_DIR/package-rpm"
VERSION="1.0.0"
RELEASE="1"
ARCH="x86_64" # Change to your architecture
PACKAGE_NAME="smfs"
RPM_ROOT="$PACKAGE_DIR/root"

echo "Preparing the RPM build directories..."
rm -rf "$PACKAGE_DIR"
mkdir -p "$RPM_ROOT/usr/local/bin"
mkdir -p "$RPM_ROOT/usr/local/include/smfs"
mkdir -p "$PACKAGE_DIR/SPECS"

echo "Copying files to the RPM root directory..."
# Copy binary
cp -r "$INSTALL_DIR/bin/"* "$RPM_ROOT/usr/local/bin"

# Copy headers
cp -r "$INSTALL_DIR/include/smfs/"* "$RPM_ROOT/usr/local/include/smfs"

echo "Creating the RPM spec file..."
cat > "$PACKAGE_DIR/SPECS/$PACKAGE_NAME.spec" <<EOF
Name: $PACKAGE_NAME
Version: $VERSION
Release: $RELEASE
Summary: Stream Master File System
License: MIT
Group: Applications/Utilities
BuildRoot: %{_topdir}/root
Prefix: /usr
%description
Stream Master File System (SMFS) - A brief description.

%files
%defattr(-,root,root)
/usr/local/bin/smfs
/usr/local/include/smfs/*
EOF

TOPDIR=$(pwd)/build/package-rpm
echo "Building the .rpm package..."
echo "Debugging Variables:"
echo "TOPDIR: $TOPDIR"
echo "PACKAGE_DIR: $PACKAGE_DIR"
echo "BUILD_DIR: $BUILD_DIR"
echo "RPM_ROOT: $RPM_ROOT"
echo "SPEC FILE: $PACKAGE_DIR/SPECS/$PACKAGE_NAME.spec"

rpmbuild --define "_topdir $TOPDIR" \
         --define "_rpmdir $BUILD_DIR" \
         --buildroot "$TOPDIR/root" \
         -bb "$TOPDIR/SPECS/$PACKAGE_NAME.spec"
         
echo "RPM package created: $BUILD_DIR/$PACKAGE_NAME-$VERSION-$RELEASE.$ARCH.rpm"
