#!/bin/bash
# Builds the Qt front and runs it with the backends of this checkout
# (CLOUDMUS_DEV_BACKENDS=1): regenerates the protocol stubs, builds the
# Spotify backend (with a local cargo, or in the pinned rust:alpine image),
# builds fronts/qt incrementally, then starts it. Arguments go to the app:
#
#   ./run-dev.sh                    # build and run
#   ./run-dev.sh --update-from=0.0.1
#
# Needs the .venv from docs/development.md (the Python backends and the
# code generator run from it).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

python=.venv/bin/python3
if [ ! -x "${python}" ]; then
    echo "no .venv: set it up first, see docs/development.md (Building from source)" >&2
    exit 1
fi

echo "==> Protocol stubs"
"${python}" protocol/codegen/generate.py --lang python rust

echo "==> Spotify backend"
if command -v cargo >/dev/null; then
    (cd backends/spotify && cargo build --release --locked)
else
    # Same image and cache as docs/development.md; the binary is static
    # musl, so it runs on the host. backends/spotify/manifest.json points
    # at backends/spotify/target/release/.
    proxy_envs=()
    for var in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
        if [ -n "${!var:-}" ]; then
            proxy_envs+=(-e "${var}=${!var}")
        fi
    done
    docker run --rm "${proxy_envs[@]}" --user "$(id -u):$(id -g)" \
        -e CARGO_HOME=/workspace/build-spotify/cargo-home \
        -e CARGO_TARGET_DIR=/workspace/backends/spotify/target \
        -v "$PWD":/workspace:z -w /workspace/backends/spotify \
        rust:1.98.0-alpine cargo build --release --locked
fi

echo "==> Qt front"
# CMake regenerates the C++ stubs itself.
if [ ! -f fronts/qt/build/CMakeCache.txt ]; then
    cmake -S fronts/qt -B fronts/qt/build
fi
cmake --build fronts/qt/build --target cloudmus-qt --parallel

echo "==> Running"
CLOUDMUS_DEV_BACKENDS=1 exec ./fronts/qt/build/bin/cloudmus-qt "$@"
