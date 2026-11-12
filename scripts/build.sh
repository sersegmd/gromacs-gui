#!/usr/bin/env bash
# Configure and build Molecular Studio.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-Release}"
cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$@"
cmake --build build -j"$(nproc)"
echo "Binary: $(pwd)/build/molecular-studio"
