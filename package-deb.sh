#!/bin/bash

set -e # Exit on any error

# Define paths and metadata
BUILD_DIR="build"
INSTALL_DIR="$BUILD_DIR/install"
PACKAGE_DIR="$BUILD_DIR/package-deb"
VERSION="1.0.0"
ARCH="amd64" # Change to your architecture
PACKAGE_NAME="smfs"
SYSTEMD_DIR="/usr/share/$PACKAGE_NAME/systemd"

echo "Preparing the Debian package directory..."
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR/DEBIAN"
mkdir -p "$PACKAGE_DIR/usr/local"
mkdir -p "$PACKAGE_DIR/$SYSTEMD_DIR"

echo "Copying files to the Debian package directory..."
cp -r "$INSTALL_DIR/"* "$PACKAGE_DIR/usr/local"
cp systemd/smfs.service "$PACKAGE_DIR/$SYSTEMD_DIR/"
cp systemd/smconfig.json "$PACKAGE_DIR/$SYSTEMD_DIR/"

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

echo "Creating postinst script..."
cat > "$PACKAGE_DIR/DEBIAN/postinst" <<EOF
#!/bin/bash
set -e

# Define paths
SERVICE_FILE="/etc/systemd/system/smfs.service"
CONFIG_FILE="/etc/smfs/smconfig.json"
SOURCE_SERVICE_FILE="$SYSTEMD_DIR/smfs.service"
SOURCE_CONFIG_FILE="$SYSTEMD_DIR/smconfig.json"

# Create the smfs user if it doesn't exist
if ! id -u smfs &>/dev/null; then
    echo "Creating user 'smfs'..."
    useradd -r -s /bin/false smfs
fi

# Ensure necessary directories exist and set correct ownership
mkdir -p /etc/smfs /var/lib/smfs /var/log/smfs
chown -R smfs:smfs /etc/smfs /var/lib/smfs /var/log/smfs

# Create the configuration directory if it doesn't exist
mkdir -p /etc/smfs

# Copy configuration file if it doesn't already exist
if [ ! -f "\$CONFIG_FILE" ]; then
    cp "\$SOURCE_CONFIG_FILE" "\$CONFIG_FILE"
    echo "Copied default config to \$CONFIG_FILE"
fi

# Copy and enable the systemd service
cp "\$SOURCE_SERVICE_FILE" "\$SERVICE_FILE"
chmod 644 "\$SERVICE_FILE"
systemctl daemon-reload
systemctl enable smfs.service

# Start the service
systemctl start smfs.service
echo "Service smfs installed and started."
echo "Don't forget to edit the configuration file at \$CONFIG_FILE"
EOF
chmod 755 "$PACKAGE_DIR/DEBIAN/postinst"

echo "Creating postrm script..."
cat > "$PACKAGE_DIR/DEBIAN/postrm" <<EOF
#!/bin/bash
set -e

# Define paths
SERVICE_FILE="/etc/systemd/system/smfs.service"
CONFIG_FILE="/etc/smfs/smconfig.json"

# Stop and disable the service
systemctl stop smfs.service || true
systemctl disable smfs.service || true

# Reload systemd to clean up
systemctl daemon-reload

# Remove service file
rm -f "\$SERVICE_FILE"
echo "Service smfs removed."

# Optionally remove configuration file during purge
if [ "\$1" = "purge" ]; then
    rm -f "\$CONFIG_FILE"
    echo "Configuration file removed."
fi

# Remove the smfs user during purge
if [ "\$1" = "purge" ]; then
    echo "Removing user 'smfs'..."
    userdel smfs || true
    rm -rf /var/lib/smfs /var/log/smfs
fi
EOF
chmod 755 "$PACKAGE_DIR/DEBIAN/postrm"

echo "Building the .deb package..."
dpkg-deb --build "$PACKAGE_DIR"

echo "Moving the package to the output directory..."
mv "$PACKAGE_DIR.deb" "$BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"

echo "Debian package created: $BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
