#!/usr/bin/env sh

set -eu

echo "Starting install of NDI to JACK for Generic ARM 32-bit..."

echo "Installing prerequisites..."
sudo bash ./preinstall.sh

echo "Extracting NDI libraries..."
sudo bash ./handle_NDI_Advanced_SDK.sh

echo "Building executable for Generic ARM 32-bit..."
sudo bash ./build_generic_armhf.sh

echo "Installing in final directory..."
sudo bash ./install.sh

echo "Done"
