#!/usr/bin/env bash
# Builds dist/CloudMus-<version>-<arch>.dmg (arch: x86_64 or arm64, this
# machine's) — CloudMus.app with Qt, libmpv, a Python runtime and the
# backends inside, here and in GitHub Actions alike. The update installer
# (fronts/qt/src/Update/InstallerMac.cpp) expects CloudMus.app at the DMG's
# root, and release.yml the file's name.
#
# Needs what docs/development.md's "macOS" section sets up: Xcode's Command
# Line Tools, the repo's .venv with cmake, ninja, meson, pkgconf, and Qt
# 6.9.3 from aqt in ~/Qt (or QT_DIR). Builds libmpv first if
# packaging/macos/deps/<arch>/ has none (build-deps.sh).
#
# Signed ad hoc: no Developer ID yet, so the first start of a downloaded
# copy needs "Open Anyway" (System Settings → Privacy & Security). Updates
# don't: the app downloads them itself, and they get no quarantine.
# CLOUDMUS_CODESIGN_IDENTITY signs with another identity; a Developer ID
# would also want the hardened runtime and notarization, not done here.
#
# Usage: packaging/macos/build-app.sh

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
cd "$repo"
arch=$(uname -m)
qt_dir=${QT_DIR:-$HOME/Qt/6.9.3/macos}
identity=${CLOUDMUS_CODESIGN_IDENTITY:--}
export MACOSX_DEPLOYMENT_TARGET=12.0
if [[ -d $repo/.venv/bin ]]; then
    export PATH=$repo/.venv/bin:$PATH
fi

# CPython for the backends: python-build-standalone, the same kind the
# AppImage bundles. name sha256 per arch.
python_release=20260901
python_version=3.13.15
case $arch in
    x86_64)
        python_triple=x86_64-apple-darwin
        python_sha=49f0d97f506b855eed60b74a8ac138595c5b39799a6aa5e0d7ca8abe1019a4d4
        ;;
    arm64)
        python_triple=aarch64-apple-darwin
        python_sha=b9054a9d3d54f4cb5573d44907fddb29874b08909bde73f29f2868cf872223ee
        ;;
    *)
        echo "build-app: unknown architecture $arch" >&2
        exit 1
        ;;
esac
backends=(local-folder yandex-music youtube-music)

# Same derivation as cmake/Version.cmake and packaging/windows/build-in-docker.sh.
version="$(git describe --tags --dirty --match '[0-9]*' --match 'v[0-9]*' 2>/dev/null || true)"
if [ -z "${version}" ]; then
    commit="$(git describe --always --dirty=.dirty 2>/dev/null || true)"
    version="0.1.0${commit:+\+g${commit}}"
fi
version="${version#v}"
# Info.plist takes numbers only: the release this build is or follows.
bundle_version=${version%%[-+]*}

deps=$here/deps/$arch
if [[ ! -f $deps/lib/libmpv.2.dylib ]]; then
    echo '==> Building libmpv'
    "$here/build-deps.sh" "$deps"
fi

echo '==> Generating protocol sources'
python3 protocol/codegen/generate.py --lang python cpp

echo '==> Building CloudMus.app'
build=$repo/build-macos-$arch
# Sentry isn't built for macOS yet: never, even with CLOUDMUS_SENTRY_DSN set.
PKG_CONFIG_PATH=$deps/lib/pkgconfig cmake -S fronts/qt -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_PREFIX_PATH="$qt_dir" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCLOUDMUS_SENTRY=OFF \
    -DCLOUDMUS_VERSION_OVERRIDE="$version"
cmake --build "$build"

stage=$repo/dist/macos-stage-$arch
app=$stage/CloudMus.app
resources=$app/Contents/Resources
rm -rf "$stage"
mkdir -p "$stage"
ditto "$build/bin/CloudMus.app" "$app"
plutil -replace CFBundleShortVersionString -string "$bundle_version" "$app/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$bundle_version" "$app/Contents/Info.plist"

echo '==> Bundling Qt and libmpv'
# Signing comes last, once everything is in.
"$qt_dir/bin/macdeployqt" "$app" -libpath="$deps/lib" -verbose=1
cp LICENSE "$resources/LICENSE.txt"
cp "$here/THIRD_PARTY.txt" "$resources/THIRD_PARTY.txt"

echo '==> Bundling Python and backends'
python_archive=$here/.cache/cpython-$python_version+$python_release-$python_triple-install_only.tar.gz
mkdir -p "$here/.cache"
if [[ ! -f $python_archive ]]; then
    curl -fsSL --retry 3 -o "$python_archive.part" \
        "https://github.com/astral-sh/python-build-standalone/releases/download/$python_release/$(basename "$python_archive")"
    mv "$python_archive.part" "$python_archive"
fi
echo "$python_sha  $python_archive" | shasum -a 256 -c -
python=$resources/python
mkdir -p "$python"
tar xzf "$python_archive" -C "$python" --strip-components=1
pylib=$python/lib/python3.13
# What no backend uses: Tk (and the Tcl/Tk libraries it loads), the test
# suite, IDLE, the headers.
rm -rf "$pylib"/{test,idlelib,tkinter,turtledemo} "$pylib"/lib-dynload/_tkinter*.so \
    "$python"/lib/{libtcl,libtk}* "$python"/lib/{tcl,tk,itcl,thread}[0-9]* "$python/include" "$python/share"
# pip's wheels name the macOS they need; one newer than the deployment
# target is caught below, by the check of every Mach-O file.
"$python/bin/python3" -m pip install --no-cache-dir --no-compile --disable-pip-version-check --quiet \
    ./backends/py-rpc-common "${backends[@]/#/./backends/}"
# Bytecode now, not on each backend's first start (the bundle is
# read-only to a user without admin rights). Unchecked-hash: valid
# whatever mtimes the copy from the DMG gives the sources.
"$python/bin/python3" -m compileall -q -j 0 --invalidation-mode unchecked-hash "$pylib"
# The interpreter is all the backends run. pip's console scripts carry
# this machine's absolute interpreter path in their #! line (broken once
# moved), and the rest is gone with what was removed above.
find "$python/bin" -mindepth 1 ! -name python3 ! -name python3.13 -delete

mkdir -p "$resources/backends"
python3 - "$resources/backends" "${backends[@]}" <<'PY'
import json
import pathlib
import sys

dest = pathlib.Path(sys.argv[1])
for backend_id in sys.argv[2:]:
    source = pathlib.Path('backends') / backend_id
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    # Relative to the manifest (Rpc::BackendManifest's bundled scan). -s:
    # no user site-packages (~/Library/Python/3.13) to shadow the bundle's.
    manifest['argv'] = ['../python/bin/python3', '-s', *manifest['argv'][1:]]
    # All the backends' files share one folder: the icon goes by the id.
    if 'icon' in manifest:
        manifest['icon'] = f'{backend_id}.svg'
        (dest / f'{backend_id}.svg').write_bytes((source / 'icon.svg').read_bytes())
    (dest / f'{backend_id}.json').write_text(json.dumps(manifest, ensure_ascii=False), encoding='utf-8')
PY

echo '==> Checking the bundle'
machos=()
while IFS= read -r -d '' file; do
    if file -b "$file" | grep -q '^Mach-O'; then
        machos+=("$file")
    fi
done < <(find "$app" -type f -print0)
failed=
for file in "${machos[@]}"; do
    # Everything outside the system comes from the bundle itself.
    while read -r dep; do
        case $dep in
            @*|/System/*|/usr/lib/*) ;;
            *) echo "build-app: ${file#$stage/} links $dep, outside the bundle" >&2; failed=1 ;;
        esac
    done < <(otool -L "$file" | tail -n +2 | awk '{print $1}')
    # ...and runs on the macOS the app says it needs.
    minos=$(otool -l "$file" | awk '/LC_BUILD_VERSION/ {b=1} b && $1 == "minos" {print $2; exit}
        /LC_VERSION_MIN_MACOSX/ {v=1} v && $1 == "version" {print $2; exit}')
    if [[ -n $minos ]] && [[ $(printf '%s\n%s\n' "$minos" "$MACOSX_DEPLOYMENT_TARGET" | sort -V | tail -1) != "$MACOSX_DEPLOYMENT_TARGET" ]]; then
        echo "build-app: ${file#$stage/} needs macOS $minos" >&2
        failed=1
    fi
done
[[ -z $failed ]]

echo '==> Signing'
# Inside out: every Mach-O file (on Apple Silicon even ad hoc is required
# to run one), the frameworks, the app. Not the app's own executable on
# its own: codesign takes that for the whole bundle, too early.
for file in "${machos[@]}"; do
    if [[ $file != "$app/Contents/MacOS/CloudMus" ]]; then
        codesign --force --sign "$identity" "$file"
    fi
done
find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0 |
    xargs -0 -n 1 codesign --force --sign "$identity"
codesign --force --sign "$identity" "$app"
codesign --verify --deep --strict "$app"

echo '==> Building the DMG'
dmg=$repo/dist/CloudMus-$version-$arch.dmg
dmg_root=$stage/dmg
mkdir -p "$dmg_root"
mv "$app" "$dmg_root/"
ln -s /Applications "$dmg_root/Applications"
rm -f "$dmg"
hdiutil create -volname CloudMus -srcfolder "$dmg_root" -fs HFS+ -format UDZO -ov "$dmg" >/dev/null
rm -rf "$stage"
echo "==> Built ${dmg#$repo/}"
