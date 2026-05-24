#!/usr/bin/env bash
# bootstrap_test.sh <dux-binary> <project-root>
# Compiles dux_stage2 and dux_stage3 from Dux source, then runs the
# stage3 round-trip suite.  Passes when stage3 reports "OVERALL: PASS".
#
# stage3 uses paths "./bootstrap/dux_stage2" and "./build/dux" relative to
# the working directory, so we run it from the project root after building
# both bootstrap binaries there.
set -euo pipefail

DUX="$1"
ROOT="$2"

STAGE2_BIN="$ROOT/bootstrap/dux_stage2"
STAGE3_BIN="$ROOT/bootstrap/dux_stage3"

echo "==> Compiling bootstrap/dux_stage2.dux ..."
"$DUX" --compile "$ROOT/bootstrap/dux_stage2.dux" -o "$STAGE2_BIN"

echo "==> Compiling bootstrap/dux_stage3.dux ..."
"$DUX" --compile "$ROOT/bootstrap/dux_stage3.dux" -o "$STAGE3_BIN"

echo "==> Running stage3 round-trip suite (from $ROOT) ..."
cd "$ROOT"
OUTPUT="$("$STAGE3_BIN" 2>&1)"
echo "$OUTPUT"

if echo "$OUTPUT" | grep -q "OVERALL: PASS"; then
    echo "Bootstrap stage3: PASS"
    exit 0
else
    echo "Bootstrap stage3: FAIL"
    exit 1
fi
