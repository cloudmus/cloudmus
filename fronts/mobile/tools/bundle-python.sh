#!/usr/bin/env bash
# CPython for the APK, with the backends: `bundle-python.sh <abi> <out dir>`.
# Leaves in <out dir>: python.zip and python.stamp (extracted at run time, see
# PythonRuntime.java), libs/ (native libraries for the APK) and include/.
# There are official Android builds for arm64-v8a and x86_64 only.
set -euo pipefail

python_version=3.14.10
abi=$1
out=$2
case $abi in
    arm64-v8a) triplet=aarch64-linux-android; sha256=423c80b957330cf0ebb92b9b20f2294c292144a441bdb0ceb8c69d14928e569b ;;
    x86_64) triplet=x86_64-linux-android; sha256=6ebda1fc3f28efc5861cad397254e8ce5d81ba4f83a3cb6a0c124c3826a0e14c ;;
    *) echo "no CPython build for $abi" >&2; exit 1 ;;
esac

root=$(cd "$(dirname "$0")/../../.." && pwd)
venv_python=$root/.venv/bin/python3
[ -x "$venv_python" ] || { echo "needs the repo's .venv (docs/development.md)" >&2; exit 1; }

here=$(cd "$(dirname "$0")" && pwd)
stamp=$( { echo "$python_version $abi"; sha256sum "$0" "$here/../python/"*;
    git -C "$root" ls-files -s backends protocol | sha256sum; } | sha256sum | cut -d' ' -f1)
if [ -f "$out/python.stamp" ] && [ "$(cat "$out/python.stamp")" = "$stamp" ]; then
    exit 0
fi

rm -rf "${out:?}" && mkdir -p "$out"
archive=$out/python.tar.gz
curl -fsSL -o "$archive" \
    "https://www.python.org/ftp/python/$python_version/python-$python_version-$triplet.tar.gz"
echo "$sha256  $archive" | sha256sum -c --quiet
mkdir "$out/unpacked"
tar xzf "$archive" -C "$out/unpacked" ./prefix
prefix=$out/unpacked/prefix
rm "$archive"

# Libraries the dynamic linker finds in the app's native library directory.
mkdir "$out/libs"
cp "$prefix"/lib/libpython3.*.so "$prefix"/lib/lib*_python.so "$out/libs/"
mv "$prefix/include" "$out/include"

# The tree under PYTHONHOME, less what a backend never needs.
tree=$out/tree
stdlib=lib/python${python_version%.*}
mkdir -p "$tree/lib"
mv "$prefix/$stdlib" "$tree/$stdlib"
(cd "$tree/$stdlib" && rm -rf test idlelib tkinter turtledemo ensurepip pydoc_data __phello__ \
    lib-dynload/_test* lib-dynload/_ctypes_test* lib-dynload/xx* config-*)
find "$tree" -name __pycache__ -type d -prune -exec rm -rf {} +
site=$tree/site-packages
mkdir "$site"

# Pure Python only: no compiler for the target here.
"$venv_python" -m pip install --quiet --target "$site" --no-deps --only-binary=:all: \
    --platform any --implementation py --abi none --python-version "${python_version%.*}" \
    certifi charset-normalizer idna mutagen PySocks requests typing-extensions urllib3 \
    "yandex-music>=3.0.0" "ytmusicapi>=1.8,<2" "yt-dlp==2026.9.27.232945.dev0"
(cd "$root" && "$venv_python" protocol/codegen/generate.py --lang python)
for package in backends/py-rpc-common/rpc_common backends/local-folder/cloudmus_backend_local \
    backends/yandex-music/cloudmus_backend_yandex backends/youtube-music/cloudmus_backend_ytmusic; do
    cp -r "$root/$package" "$site/"
done
find "$site" -name __pycache__ -type d -prune -exec rm -rf {} +
find "$site" -maxdepth 1 -name '*.dist-info' -type d -exec rm -rf {} +
rm -rf "${site:?}/bin"
cp "$here/../python/"* "$tree/"

(cd "$tree" && "$venv_python" -c "
import os, zipfile
with zipfile.ZipFile('../python.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk('.'):
        for f in sorted(files):
            z.write(os.path.join(d, f))
")
rm -rf "${out:?}/unpacked" "${tree:?}"
echo "$stamp" > "$out/python.stamp"
