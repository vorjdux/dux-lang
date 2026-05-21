#!/usr/bin/env bash
# Quick test runner — builds if needed, then runs ctest with verbose output.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"

if [[ ! -f "${BUILD_DIR}/dux" ]]; then
    echo "==> Configuring..."
    cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug "${REPO_ROOT}"
fi

echo "==> Building..."
cmake --build "${BUILD_DIR}" -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo "==> Running tests..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure "$@"
