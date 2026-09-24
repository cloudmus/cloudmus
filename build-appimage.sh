#!/bin/bash
# Builds a self-contained Linux AppImage for cloudmus-qt, bundling its
# Python backends too — see packaging/appimage/ for the full design
# rationale (Debian 11 base for an old glibc floor, Qt6 via aqtinstall
# since bullseye has no Qt6 packages, bundled Python runtime, etc.).
#
# Also run unmodified by .github/workflows/release.yml on an
# ubuntu-latest runner (Docker is preinstalled there).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

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

docker build \
    "${proxy_build_args[@]}" \
    -t cloudmus-appimage-builder \
    -f packaging/appimage/Dockerfile \
    packaging/appimage

mkdir -p dist

docker run --rm \
    "${proxy_run_envs[@]}" \
    -v "$PWD":/workspace:z \
    -w /workspace \
    cloudmus-appimage-builder \
    bash packaging/appimage/build-in-docker.sh

echo "Built: dist/CloudMus-x86_64.AppImage"
