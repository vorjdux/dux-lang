#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$SCRIPT_DIR/.."
DUX="$REPO/build/dux"

if [ ! -x "$DUX" ]; then
    echo "Error: dux compiler not found at $DUX"
    echo "Build it first: cd build && make"
    exit 1
fi

echo "Stage 1: Compiling dux_stage2.dux with C++ dux compiler..."
"$DUX" --compile "$SCRIPT_DIR/dux_stage2.dux" -o "$SCRIPT_DIR/dux_stage2"
echo "Stage 2 binary: $SCRIPT_DIR/dux_stage2"

echo ""
echo "Testing: dumping AST of hello_world.dux..."
"$SCRIPT_DIR/dux_stage2" --dump-ast "$REPO/examples/hello_world.dux"
