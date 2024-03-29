#!/usr/bin/env sh

INSTALL_DIR="/opt/ndi2jack"
BIN_DIR="$INSTALL_DIR/bin"
ASSETS_DIR="$INSTALL_DIR/assets"
LIB_DIR="/usr/lib"

rm -R "$INSTALL_DIR"
rm -R "$LIB_DIR/libndi*"

if [ ! -d "$INSTALL_DIR" ]; then
  mkdir "$INSTALL_DIR"
fi

if [ ! -d "$LIB_DIR" ]; then
  mkdir "$LIB_DIR"
fi

if [ ! -d "$BIN_DIR" ]; then
  mkdir "$BIN_DIR"
fi

if [ ! -d "$ASSETS_DIR" ]; then
  mkdir "$ASSETS_DIR"
fi

cp lib/* "$LIB_DIR"

cp build/ndi2jack "$BIN_DIR"
cp build/jack2ndi "$BIN_DIR"

cp assets/* "$ASSETS_DIR"

chmod +x "$BIN_DIR/ndi2jack"
chmod +x "$BIN_DIR/jack2ndi"

#symlink to the /usr/bin directory
ln -s "$BIN_DIR/ndi2jack" /usr/bin/
ln -s "$BIN_DIR/jack2ndi" /usr/bin/