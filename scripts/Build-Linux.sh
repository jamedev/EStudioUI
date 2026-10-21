#!/usr/bin/env bash
set -euo pipefail
projectRoot="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
configuration="${1:-Release}"
case "$configuration" in Debug|Release) ;; *) echo 'Usage: Build-Linux.sh [Debug|Release] [--run]' >&2; exit 2;; esac
: "${VCPKG_ROOT:?Set VCPKG_ROOT to a bootstrapped vcpkg Git checkout.}"
buildDirectory="$projectRoot/build/linux-${configuration,,}"
cmake -S "$projectRoot" -B "$buildDirectory" -G Ninja \
    -DCMAKE_BUILD_TYPE="$configuration" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build "$buildDirectory" --parallel "${ESTUDIOUI_BUILD_JOBS:-2}"
ctest --test-dir "$buildDirectory" --output-on-failure --no-tests=error
if [[ "${2:-}" == '--run' ]]; then
    exec "$buildDirectory/EStudioUIDemo"
fi
printf 'Built %s\n' "$buildDirectory/EStudioUIDemo"
