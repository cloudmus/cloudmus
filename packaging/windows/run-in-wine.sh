#!/bin/bash
# Runs the Windows build under the host's Wine, in a prefix of its own
# (the default ~/.wine is left alone): dist/windows-stage/ (Qt, Python,
# backends — from ./build-windows.sh) with build-windows/bin/cloudmus-qt.exe
# (possibly newer, from build-exe-in-docker.sh) over it.
#
#   packaging/windows/run-in-wine.sh                 # on your display
#   packaging/windows/run-in-wine.sh --shot out.png  # headless: Xvfb, one
#                                                    # screenshot, then quit
# Env: CLOUDMUS_WINE_DIR (default /tmp/cloudmus-wine) holds the copy and the
# prefix; SHOT_DELAY (seconds, default 25) before the screenshot.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

work_dir="${CLOUDMUS_WINE_DIR:-/tmp/cloudmus-wine}"
shot=""
if [ "${1:-}" = "--shot" ]; then
    shot="$(realpath -m "${2:?--shot needs a file}")"
fi

if [ ! -d dist/windows-stage ]; then
    echo "no dist/windows-stage: run ./build-windows.sh once" >&2
    exit 1
fi
mkdir -p "${work_dir}"
rsync -a --delete dist/windows-stage/ "${work_dir}/app/"
if [ build-windows/bin/cloudmus-qt.exe -nt dist/windows-stage/cloudmus-qt.exe ]; then
    cp build-windows/bin/cloudmus-qt.exe "${work_dir}/app/"
fi

export WINEPREFIX="${work_dir}/prefix" WINEDEBUG="${WINEDEBUG:--all}" CLOUDMUS_QT_DEBUG=1
if [ -n "${shot}" ]; then
    display=:$((90 + RANDOM % 100))
    Xvfb "${display}" -screen 0 1600x1000x24 >/dev/null 2>&1 &
    xvfb_pid=$!
    trap 'wineserver -k 2>/dev/null || true; kill "${xvfb_pid}" 2>/dev/null || true' EXIT
    export DISPLAY="${display}"
    unset WAYLAND_DISPLAY
    sleep 1
fi
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1

cd "${work_dir}/app"
if [ -z "${shot}" ]; then
    exec wine cloudmus-qt.exe "$@"
fi
wine cloudmus-qt.exe >"${work_dir}/run.log" 2>&1 &
sleep "${SHOT_DELAY:-25}"
import -window root "${shot}"
echo "screenshot: ${shot}; log: ${work_dir}/run.log"
