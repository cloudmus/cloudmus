#!/bin/bash
# Builds the Spotify backend (backends/spotify, Rust) for packaging, in the
# same pinned rust:alpine image as the AppImage's AppRun:
#
#   packaging/build-spotify.sh linux     # -> build-spotify/target/release/cloudmus-backend-spotify
#   packaging/build-spotify.sh windows   # -> build-spotify/target/x86_64-pc-windows-gnu/release/cloudmus-backend-spotify.exe
#
# On Linux the binary is static musl, so nothing it needs can be missing on
# the host; for Windows it is cross-built with MinGW and needs only the
# system DLLs (the UCRT included, part of Windows 10 and later). Either way
# build-spotify/THIRD_PARTY-spotify.txt gets the licenses of the Rust crates
# it is built from, to ship next to it.
#
# Runs as root in the container (it installs the generator's Python and the
# MinGW toolchain), then hands what it wrote back to the invoking user.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

case "${1:-}" in
    linux) target="" platform=x86_64-unknown-linux-musl packages="python3 py3-jinja2 py3-yaml" ;;
    windows) target=x86_64-pc-windows-gnu platform=x86_64-pc-windows-gnu packages="python3 py3-jinja2 py3-yaml mingw-w64-gcc" ;;
    *) echo "usage: $0 linux|windows" >&2; exit 2 ;;
esac

proxy_run_envs=()
for var in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
    if [ -n "${!var:-}" ]; then
        proxy_run_envs+=(-e "${var}=${!var}")
    fi
done

mkdir -p build-spotify
docker run --rm \
    "${proxy_run_envs[@]}" \
    -e CARGO_HOME=/workspace/build-spotify/cargo-home \
    -e CARGO_TARGET_DIR=/workspace/build-spotify/target \
    -e TARGET="${target}" \
    -e PLATFORM="${platform}" \
    -e PACKAGES="${packages}" \
    -e OWNER="$(id -u):$(id -g)" \
    -v "$PWD":/workspace:z \
    -w /workspace \
    rust:1.98.0-alpine \
    sh -euc '
        trap "chown -R \"\$OWNER\" /workspace/build-spotify /workspace/backends/spotify/src/generated 2>/dev/null || true" EXIT
        apk add --no-cache $PACKAGES >/dev/null
        if [ -n "$TARGET" ]; then rustup target add "$TARGET" >/dev/null; fi
        python3 protocol/codegen/generate.py --lang rust
        cd backends/spotify
        cargo build --release --locked ${TARGET:+--target "$TARGET"}
        cargo metadata --format-version 1 --locked --filter-platform "$PLATFORM" > /tmp/metadata.json
        python3 /workspace/packaging/spotify-licenses.py /tmp/metadata.json \
            > /workspace/build-spotify/THIRD_PARTY-spotify.txt
    '
