#!/bin/bash
set -euo pipefail

repo_dir="$(pwd)"
stage_dir="${repo_dir}/dist/windows-stage"
build_dir="${repo_dir}/build-windows"
qt_dir=/opt/qt/6.9.3/mingw_64
mingw_dir=/opt/qt/Tools/mingw1310_64
cmake_exe=/opt/windows-cmake/bin/cmake.exe
qt_deploy_exe="${qt_dir}/bin/windeployqt.exe"

git config --global --add safe.directory "${repo_dir}"
version="$(git describe --tags --dirty --match '[0-9]*' --match 'v[0-9]*' 2>/dev/null || true)"
if [ -z "${version}" ]; then
    commit="$(git describe --always --dirty=.dirty 2>/dev/null || true)"
    version="0.1.0${commit:+\+g${commit}}"
fi
version="${version#v}"

echo '==> Generating protocol sources'
python3 protocol/codegen/generate.py --lang python cpp

echo '==> Building Windows executable'
export WINEPATH="$(winepath -w "${mingw_dir}/bin");$(winepath -w "${qt_dir}/bin");$(winepath -w /opt/windows-ninja)"
repo_win="$(winepath -w "${repo_dir}")"
build_win="$(winepath -w "${build_dir}")"
stage_win="$(winepath -w "${stage_dir}")"
qt_win="$(winepath -w "${qt_dir}")"
mpv_win="$(winepath -w /opt/mpv)"
ninja_win="$(winepath -w /opt/windows-ninja/ninja.exe)"
mkdir -p "${build_dir}"
xvfb-run -a wine "${cmake_exe}" -S "${repo_win}\\fronts\\qt" -B "${build_win}" \
    -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_C_FLAGS=-g1 -DCMAKE_CXX_FLAGS=-g1 \
    "-DFETCHCONTENT_SOURCE_DIR_SENTRY-NATIVE=$(winepath -w /opt/sentry-native)" \
    -DCMAKE_MAKE_PROGRAM="${ninja_win}" \
    -DCMAKE_PREFIX_PATH="${qt_win}" \
    -DCLOUDMUS_MPV_ROOT="${mpv_win}" \
    -DCLOUDMUS_PREGENERATED_PROTOCOL=ON \
    -DCLOUDMUS_VERSION_OVERRIDE="${version}"
xvfb-run -a wine "${cmake_exe}" --build "${build_win}" --target cloudmus-qt cloudmus-yt-dlp --parallel 4

# The exe that ships is this one, unstripped; the copy is for CI to upload
# to Sentry (-g1 DWARF, matched by build id).
mkdir -p "${repo_dir}/dist/symbols"
cp "${build_dir}/bin/cloudmus-qt.exe" "${repo_dir}/dist/symbols/cloudmus-qt.exe"

echo '==> Bundling Qt and libmpv'
rm -rf "${stage_dir}"
mkdir -p "${stage_dir}" "${stage_dir}/python/Lib/site-packages" "${stage_dir}/backends"
cp "${build_dir}/bin/cloudmus-qt.exe" "${stage_dir}/"
cp "${build_dir}/bin/yt-dlp.exe" "${stage_dir}/"
cp /opt/mpv/libmpv-2.dll "${stage_dir}/"
cp /opt/vulkan-rt/vulkan-1.dll "${stage_dir}/"
cp /opt/vulkan-rt/VulkanRT-License.txt "${stage_dir}/"
cp LICENSE "${stage_dir}/LICENSE.txt"
cp packaging/windows/THIRD_PARTY.txt "${stage_dir}/"
xvfb-run -a wine "${qt_deploy_exe}" --release --translations ru,fr,es,de,it \
    --compiler-runtime --dir "${stage_win}" "${stage_win}\\cloudmus-qt.exe"

echo '==> Bundling Python and backends'
unzip -q /opt/python-embed.zip -d "${stage_dir}/python"
cat > "${stage_dir}/python/python313._pth" <<'EOF'
python313.zip
.
Lib\site-packages
import site
EOF
wheel_dir="$(mktemp -d)"
trap 'rm -rf "${wheel_dir}"' EXIT
pip wheel --no-deps --wheel-dir "${wheel_dir}" \
    ./backends/py-rpc-common ./backends/local-folder \
    ./backends/yandex-music ./backends/youtube-music
python3 packaging/windows/build-python-env.py \
    "${stage_dir}/python/Lib/site-packages" "${wheel_dir}"
# Without bytecode every fresh install compiles all of site-packages on each
# backend's first start (under the virus scanner, too). The bundled 3.13
# must do it — the host Python is another version — and unchecked-hash
# pycs stay valid whatever mtimes the installer gives the sources.
xvfb-run -a wine "${stage_dir}/python/python.exe" -m compileall -q -j 0 \
    --invalidation-mode unchecked-hash "$(winepath -w "${stage_dir}/python/Lib/site-packages")"

python3 - "${stage_dir}/backends" <<'PY'
import json
import pathlib
import sys

dest = pathlib.Path(sys.argv[1])
for backend_id in ('local-folder', 'yandex-music', 'youtube-music'):
    source = pathlib.Path('backends') / backend_id
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    manifest['argv'] = ['../python/python.exe', *manifest['argv'][1:]]
    # manifest['icon'] is "icon.svg", resolved relative to the manifest's
    # own directory (Rpc::BackendManifest::parseManifest) — on Linux/dev
    # that's backends/<id>/icon.svg, next to backends/<id>/manifest.json,
    # but here every backend's files land flattened into one directory, so
    # the icon has to be renamed to match, or that lookup silently misses.
    if 'icon' in manifest:
        manifest['icon'] = f'{backend_id}.svg'
    (dest / f'{backend_id}.json').write_text(json.dumps(manifest, ensure_ascii=False), encoding='utf-8')
    (dest / f'{backend_id}.svg').write_bytes((source / 'icon.svg').read_bytes())
PY

echo '==> Building NSIS installer'
bitmaps_dir="${build_dir}/installer-bitmaps"
packaging/windows/make-installer-bitmaps.sh "${bitmaps_dir}"
installer="${repo_dir}/dist/CloudMus-${version}-x86_64-Setup.exe"
makensis -V2 -DVERSION="${version}" -DSTAGING="${stage_dir}" \
    -DCLOUDMUS_ICON="${repo_dir}/packaging/windows/cloudmus.ico" \
    -DBITMAPS="${bitmaps_dir}" \
    -DOUTPUT="${installer}" \
    packaging/windows/cloudmus.nsi
echo "==> Built ${installer#${repo_dir}/}"
