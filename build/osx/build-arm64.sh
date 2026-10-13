#!/bin/bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 QT_PREFIX BUILD_DIR [CRYPTOPP_PREFIX]" >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")/../.." && pwd)
qt_prefix=$1
build_dir=$2
crypto_prefix=${3:-/opt/homebrew}
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_PREFIX_PATH="$qt_prefix;$crypto_prefix" \
    -DWIZNOTE_USE_CONAN=OFF -DBUILD_TESTING=OFF \
    -DWIZNOTE_BUILD_COMPAT_TESTS=ON \
    -DWIZNOTE_QT_HOST_TOOLS_ROSETTA="${WIZNOTE_QT_HOST_TOOLS_ROSETTA:-OFF}"
cmake --build "$build_dir" --parallel "${WIZNOTE_BUILD_JOBS:-8}"
ctest --test-dir "$build_dir" --output-on-failure
