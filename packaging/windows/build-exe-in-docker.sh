#!/bin/bash
# Rebuilds just cloudmus-qt.exe in build-windows/, incrementally — for
# checking a change compiles for Windows without a whole installer build.
# Needs build-windows/ configured by one full ./build-windows.sh first.
# Run from the repo root:
#   docker run --rm -v "$PWD":/workspace:z -w /workspace \
#       cloudmus-windows-builder bash packaging/windows/build-exe-in-docker.sh
set -euo pipefail

repo_dir="$(pwd)"
build_dir="${repo_dir}/build-windows"
qt_dir=/opt/qt/6.9.3/mingw_64
mingw_dir=/opt/qt/Tools/mingw1310_64

if [ ! -f "${build_dir}/CMakeCache.txt" ]; then
    echo "build-windows/ isn't configured: run ./build-windows.sh once" >&2
    exit 1
fi
git config --global --add safe.directory "${repo_dir}"
python3 protocol/codegen/generate.py --lang cpp >/dev/null

export WINEPATH="$(winepath -w "${mingw_dir}/bin");$(winepath -w "${qt_dir}/bin");$(winepath -w /opt/windows-ninja)"
# Wine's own noise (fixme:/err: lines) buries the compiler's.
xvfb-run -a wine /opt/windows-cmake/bin/cmake.exe --build "$(winepath -w "${build_dir}")" \
    --target cloudmus-qt --parallel "$(nproc)" 2>&1 | grep -v -E '^[0-9a-f]+:(fixme|err|warn):'
exit "${PIPESTATUS[0]}"
