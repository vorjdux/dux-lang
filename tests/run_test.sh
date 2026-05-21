#!/usr/bin/env bash
# run_test.sh <dux-binary> <source.dux> <expected-output>
# Compiles source, runs the binary, diffs against expected.
set -euo pipefail

DUX="$1"
SRC="$2"
EXPECTED="$3"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

BIN="$TMPDIR/prog"

# Compile to native binary
"$DUX" --compile "$SRC" -o "$BIN"

# Run and capture output
ACTUAL="$("$BIN" 2>&1)"
EXPECT="$(cat "$EXPECTED")"

if [ "$ACTUAL" = "$EXPECT" ]; then
    exit 0
else
    echo "EXPECTED:"
    echo "$EXPECT"
    echo "ACTUAL:"
    echo "$ACTUAL"
    exit 1
fi
