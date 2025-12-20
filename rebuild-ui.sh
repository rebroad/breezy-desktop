#!/bin/bash
# Quick rebuild script for Breezy Desktop UI
# Run this after making changes to UI files or Python code

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UI_DIR="$SCRIPT_DIR/ui"
BUILD_DIR="$UI_DIR/build"
INSTALL_DATA_DIR="$HOME/.local/share/breezydesktop"
INSTALL_BIN_DIR="$HOME/.local/bin"

echo "🔨 Building Breezy Desktop UI..."

# Setup build if needed
if [ ! -d "$BUILD_DIR" ]; then
    echo "📦 Setting up Meson build..."
    cd "$UI_DIR"
    meson setup build
fi

# Compile
cd "$UI_DIR"
meson compile -C build

echo "📦 Installing to ~/.local/..."

# Copy GResource bundle
mkdir -p "$INSTALL_DATA_DIR"
cp "$BUILD_DIR/src/breezydesktop.gresource" "$INSTALL_DATA_DIR/" && echo "  ✓ GResource bundle"

# Copy binary
mkdir -p "$INSTALL_BIN_DIR"
cp "$BUILD_DIR/src/breezydesktop" "$INSTALL_BIN_DIR/" && echo "  ✓ Binary"

# Copy Python modules
mkdir -p "$INSTALL_DATA_DIR/breezydesktop"
cp -r "$UI_DIR/src"/*.py "$INSTALL_DATA_DIR/breezydesktop/" 2>/dev/null || true
if [ -d "$UI_DIR/lib" ]; then
    cp -r "$UI_DIR/lib" "$INSTALL_DATA_DIR/breezydesktop/" 2>/dev/null || true
fi
if [ -f "$SCRIPT_DIR/modules/PyXRLinuxDriverIPC/xrdriveripc.py" ]; then
    cp -L "$SCRIPT_DIR/modules/PyXRLinuxDriverIPC/xrdriveripc.py" "$INSTALL_DATA_DIR/breezydesktop/" 2>/dev/null || true
fi
echo "  ✓ Python modules"

echo "✅ Done! Run 'breezydesktop' to test."

