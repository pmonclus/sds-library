#!/bin/bash
# generate.sh - Set up the hybrid demo example
#
# This script:
#   1. Copies the SDS library files to lib/ (self-contained)
#   2. Generates demo_types.h and demo_types.py from schema.sds

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SDS_ROOT="$SCRIPT_DIR/../.."
CODEGEN="$SDS_ROOT/tools/sds_codegen.py"

if [ ! -f "$CODEGEN" ]; then
    echo "Error: Code generator not found at $CODEGEN"
    echo "Make sure you're running from the hybrid_demo directory"
    exit 1
fi

# Step 1: Copy SDS library files
echo "Copying SDS library files..."
mkdir -p "$SCRIPT_DIR/lib/include" "$SCRIPT_DIR/lib/src" "$SCRIPT_DIR/lib/platform/posix" "$SCRIPT_DIR/lib/platform/esp32"
cp "$SDS_ROOT/include/sds.h" "$SDS_ROOT/include/sds_json.h" "$SDS_ROOT/include/sds_error.h" "$SDS_ROOT/include/sds_platform.h" "$SCRIPT_DIR/lib/include/"
cp "$SDS_ROOT/src/sds_core.c" "$SDS_ROOT/src/sds_json.c" "$SCRIPT_DIR/lib/src/"
cp "$SDS_ROOT/platform/posix/sds_platform_posix.c" "$SCRIPT_DIR/lib/platform/posix/"
cp "$SDS_ROOT/platform/esp32/sds_platform_esp32.cpp" "$SCRIPT_DIR/lib/platform/esp32/"

# Step 2: Generate types
echo "Generating types from schema.sds..."

# Generate C header
python3 "$CODEGEN" "$SCRIPT_DIR/schema.sds" --c -o "$SCRIPT_DIR/lib/include"
mv "$SCRIPT_DIR/lib/include/sds_types.h" "$SCRIPT_DIR/lib/include/demo_types.h"

# Generate Python types
python3 "$CODEGEN" "$SCRIPT_DIR/schema.sds" --python -o "$SCRIPT_DIR/python_owner"
mv "$SCRIPT_DIR/python_owner/sds_types.py" "$SCRIPT_DIR/python_owner/demo_types.py"

# Fix the Python import (sds_types -> demo_types doesn't need fixing, but update the header comment)
sed -i.bak 's/sds_types.py/demo_types.py/g' "$SCRIPT_DIR/python_owner/demo_types.py"
rm -f "$SCRIPT_DIR/python_owner/demo_types.py.bak"

echo ""
echo "Generated:"
echo "  - lib/include/demo_types.h"
echo "  - python_owner/demo_types.py"
echo ""
echo "You can now build the devices and run the demo."
