#!/bin/bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

proxy_build_args=()
proxy_run_envs=()
for var in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
    if [ -n "${!var:-}" ]; then
        proxy_build_args+=(--build-arg "${var}=${!var}")
        proxy_run_envs+=(-e "${var}=${!var}")
    fi
done

ga4_run_envs=()
if [ -n "${CLOUDMUS_GA4_MEASUREMENT_ID:-}" ]; then
    ga4_run_envs+=(-e "CLOUDMUS_GA4_MEASUREMENT_ID")
fi

sentry_run_envs=()
if [ -n "${CLOUDMUS_SENTRY_DSN:-}" ]; then
    sentry_run_envs+=(-e "CLOUDMUS_SENTRY_DSN")
fi

docker build "${proxy_build_args[@]}" -t cloudmus-windows-builder \
    -f packaging/windows/Dockerfile packaging/windows
mkdir -p dist
docker run --rm "${proxy_run_envs[@]}" "${ga4_run_envs[@]}" "${sentry_run_envs[@]}" \
    -v "$PWD":/workspace:z -w /workspace cloudmus-windows-builder \
    bash packaging/windows/build-in-docker.sh
