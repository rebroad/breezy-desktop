#!/usr/bin/env bash
# Script to update .pot file only if source files are newer

set -e

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
UI_DIR="$SCRIPT_DIR/.."
POT_FILE="$UI_DIR/po/breezydesktop.pot"

# Find all source files
PYTHON_FILES=$(find "$UI_DIR/src" -name "*.py" -type f | sort)
UI_FILES=$(find "$UI_DIR/src/gtk" -name "*.ui" -type f | sort)
XRD_FILES=$(find "$UI_DIR/modules/PyXRLinuxDriverIPC" -name "*.py" -type f | sort)

ALL_SOURCES="$PYTHON_FILES $UI_FILES $XRD_FILES"

# Check if .pot file exists
if [ ! -f "$POT_FILE" ]; then
    echo "Creating new .pot file..."
    NEEDS_UPDATE=true
else
    # Check if any source file is newer than .pot
    POT_MTIME=$(stat -c %Y "$POT_FILE" 2>/dev/null || echo 0)
    NEEDS_UPDATE=false

    for source_file in $ALL_SOURCES; do
        if [ -f "$source_file" ]; then
            SOURCE_MTIME=$(stat -c %Y "$source_file" 2>/dev/null || echo 0)
            if [ "$SOURCE_MTIME" -gt "$POT_MTIME" ]; then
                NEEDS_UPDATE=true
                break
            fi
        fi
    done
fi

if [ "$NEEDS_UPDATE" = true ]; then
    echo "Source files changed, updating .pot file..."
    cd "$UI_DIR"

    # Extract Python strings
    xgettext --from-code=UTF-8 -k_ -kN_ -L Python -o po/breezydesktop.pot src/*.py modules/PyXRLinuxDriverIPC/*.py 2>/dev/null || true

    # Add UI strings (join with existing)
    xgettext --from-code=UTF-8 -k_ -kN_ -j -L Glade -o po/breezydesktop.pot src/gtk/*.ui 2>/dev/null || true

    echo ".pot file updated"
else
    echo ".pot file is up to date"
fi

# Always touch output file to satisfy meson's dependency tracking
# Meson passes the output file path as the first argument
if [ -n "$1" ]; then
    touch "$1" 2>/dev/null || true
fi

