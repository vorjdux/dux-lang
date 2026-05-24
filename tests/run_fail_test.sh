#!/usr/bin/env bash
# run_fail_test.sh <dux-binary> <source.dux>
# Compiles source; the resulting binary must exit non-zero (runtime failure).
set -euo pipefail

DUX="$1"
SRC="$2"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

BIN="$TMPDIR/prog"

# Compile must succeed
"$DUX" --compile "$SRC" -o "$BIN"

# Running must exit non-zero
if "$BIN" >/dev/null 2>&1; then
    echo "FAIL: program exited 0 but expected non-zero exit"
    exit 1
else
    exit 0
fi
