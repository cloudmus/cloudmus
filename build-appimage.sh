#!/bin/bash
# Builds a self-contained Linux AppImage for cloudmus-qt, bundling its
# Python backends too — see packaging/appimage/ for the full design
# rationale (Debian 11 base for an old glibc floor, Qt6 via aqtinstall
# since bullseye has no Qt6 packages, bundled Python runtime, etc.).
#
# Also run unmodified by .github/workflows/release.yml on an
# ubuntu-latest runner (Docker is preinstalled there).
#
# --asan builds a diagnostic variant instead (CloudMus-<version>-asan-
# x86_64.AppImage): cloudmus-qt under AddressSanitizer, with the same
# compiler and bundled libraries as the release — for memory bugs that
# only the AppImage shows. Never for publishing.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

asan_run_envs=()
for arg in "$@"; do
    case "$arg" in
        --asan) asan_run_envs=(-e CLOUDMUS_APPIMAGE_ASAN=1) ;;
        *) echo "usage: $0 [--asan]" >&2; exit 2 ;;
    esac
done

# Forward the invoking shell's own proxy settings into both the image
# build and the build-in-docker.sh run — several hosts this build needs
# (download.qt.io, Qt mirrors, GitHub releases, PyPI) can be
# region-blocked/throttled on some networks, and a proxy already set up
# for normal browsing on this machine should also cover Docker's
# outbound traffic. A no-op (empty arrays) when none of these are set.
proxy_build_args=()
proxy_run_envs=()
for var in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
    if [ -n "${!var:-}" ]; then
        proxy_build_args+=(--build-arg "${var}=${!var}")
        proxy_run_envs+=(-e "${var}=${!var}")
    fi
done

# These values become part of the public AppImage, so Docker receives them
# only for the build run, never as image build arguments.
ga4_run_envs=()
for var in CLOUDMUS_GA4_MEASUREMENT_ID; do
    if [ -n "${!var:-}" ]; then
        ga4_run_envs+=(-e "$var")
    fi
done

# The DSN ends up in the binary too (it is a public client key).
sentry_run_envs=()
if [ -n "${CLOUDMUS_SENTRY_DSN:-}" ]; then
    sentry_run_envs+=(-e CLOUDMUS_SENTRY_DSN)
fi

# AppRun is a static Rust binary (packaging/appimage/apprun), built apart from
# the rest: the builder image's Debian 11 has a Rust too old for the crates
# and no musl target, while the official rust:alpine image is musl-native, so
# a plain release build comes out fully static. The tag is pinned exactly.
mkdir -p build-apprun
docker run --rm \
    "${proxy_run_envs[@]}" \
    --user "$(id -u):$(id -g)" \
    -e CARGO_HOME=/workspace/build-apprun/cargo-home \
    -e CARGO_TARGET_DIR=/workspace/build-apprun/target \
    -v "$PWD":/workspace:z \
    -w /workspace/packaging/appimage/apprun \
    rust:1.98.0-alpine \
    cargo build --release --locked

docker build \
    "${proxy_build_args[@]}" \
    -t cloudmus-appimage-builder \
    -f packaging/appimage/Dockerfile \
    packaging/appimage

mkdir -p dist

docker run --rm \
    "${proxy_run_envs[@]}" \
    "${ga4_run_envs[@]}" \
    "${sentry_run_envs[@]}" \
    "${asan_run_envs[@]}" \
    -v "$PWD":/workspace:z \
    -w /workspace \
    cloudmus-appimage-builder \
    bash packaging/appimage/build-in-docker.sh
