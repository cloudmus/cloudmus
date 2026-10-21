#!/usr/bin/env bash
# Builds libmpv and everything it links from pinned sources into
# packaging/macos/deps/<arch>/ — for a dev build of fronts/qt and for the
# app bundle alike, here and in GitHub Actions.
#
# Not from Homebrew or MacPorts: their dylibs are built for the macOS of the
# machine that installed them (on a runner, the newest one) and drag a tree
# of other dylibs along. Here everything but libmpv itself is a static
# library, so the bundle ships one libmpv.2.dylib, and all of it targets
# MACOSX_DEPLOYMENT_TARGET.
#
# Needs only Xcode's Command Line Tools and the build tools from the repo's
# .venv (pip install meson ninja pkgconf; see docs/development.md). A library
# already built is skipped, so a rerun after a failure resumes; bumping a
# version means deleting packaging/macos/deps/<arch>/ (CI keys its cache on
# this file's hash).
#
# Usage: packaging/macos/build-deps.sh [PREFIX]

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
arch=$(uname -m)
prefix=${1:-$here/deps/$arch}
cache=$here/.cache
work=$here/.cache/build-$arch

export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-12.0}
if [[ -d $repo/.venv/bin ]]; then
    export PATH=$repo/.venv/bin:$PATH
fi
export PKG_CONFIG_PATH=$prefix/lib/pkgconfig
jobs=$(sysctl -n hw.ncpu)

for tool in meson ninja pkg-config; do
    command -v "$tool" >/dev/null || { echo "build-deps: $tool not found (pip install meson ninja pkgconf)" >&2; exit 1; }
done

# name version url sha256
sources=(
    "ffmpeg 8.1 https://ffmpeg.org/releases/ffmpeg-8.1.tar.xz b072aed6871998cce9b36e7774033105ca29e33632be5b6347f3206898e0756a"
    "freetype 2.14.3 https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz 36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f"
    "fribidi 1.0.16 https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz 1b1cde5b235d40479e91be2f0e88a309e3214c8ab470ec8a2744d82a5a9ea05c"
    "harfbuzz 14.4.0 https://github.com/harfbuzz/harfbuzz/releases/download/14.4.0/harfbuzz-14.4.0.tar.xz 2357ed966c6ced7bfa720b0640c0231065af01158fbea215093ffa15aed44371"
    "libass 0.17.5 https://github.com/libass/libass/releases/download/0.17.5/libass-0.17.5.tar.xz 2dca25c0e0c837ddf00b52011b3f82cac1e4ddd3ad018227806b0c2288864acc"
    "vulkan_headers 1.4.360 https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.360.tar.gz 8f01247c7bbe3d1eb3fee60e19810b1503486b4342535d5e689d940b4d9414da"
    "fast_float 8.2.10 https://github.com/fastfloat/fast_float/archive/refs/tags/v8.2.10.tar.gz 76f958dd97b1cf4d8862d1f0986a47d4bdfa8845252bae15ef0f40de3b95961f"
    "libplacebo 7.360.1 https://github.com/haasn/libplacebo/archive/refs/tags/v7.360.1.tar.gz d05fdf90bea2f629eaa2d115e909fd356388ac639e54f77b87a018a6d76224bd"
    "mpv 0.41.0 https://github.com/mpv-player/mpv/archive/refs/tags/v0.41.0.tar.gz ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209"
)

# Downloads and unpacks one source into $work/<name>-<version>, prints the path.
fetch() {
    local name=$1 version=$2 url=$3 sha=$4
    local archive=$cache/$name-$version.tar.${url##*.tar.}
    if [[ ! -f $archive ]]; then
        curl -fL --retry 3 -o "$archive.part" "$url" >&2
        mv "$archive.part" "$archive"
    fi
    echo "$sha  $archive" | shasum -a 256 -c - >&2
    local dir=$work/$name-$version
    rm -rf "$dir"
    mkdir -p "$dir"
    tar xf "$archive" -C "$dir" --strip-components=1
    echo "$dir"
}

stamp() { echo "$prefix/.built-$1-$2"; }

# meson for all but ffmpeg: static, position-independent (it ends up inside
# libmpv.dylib), release, into $prefix.
meson_build() {
    local src=$1
    shift
    meson setup "$src/_build" "$src" --prefix="$prefix" --libdir=lib --buildtype=release \
        -Ddefault_library=static -Db_staticpic=true -Dprefer_static=true "$@"
    meson compile -C "$src/_build"
    meson install -C "$src/_build"
}

build_ffmpeg() {
    # LGPL only (no --enable-gpl): what the MIT app may bundle. Audio is all
    # the app plays, but the decoders and demuxers stay whole — a local
    # folder can hold any format, and mpv's audio chain uses lavfi filters.
    # TLS through Secure Transport: the system's own trust store, no CA file.
    # No x86 assembly: it needs nasm, and audio decoding doesn't miss it.
    local asm=()
    [[ $arch == x86_64 ]] && asm=(--disable-x86asm)
    (cd "$1" && ./configure --prefix="$prefix" --enable-static --disable-shared --enable-pic \
        --disable-programs --disable-doc --disable-debug --disable-avdevice \
        --disable-encoders --disable-muxers --disable-indevs --disable-outdevs \
        --disable-sdl2 --disable-xlib --disable-libxcb --disable-vulkan \
        --enable-securetransport "${asm[@]}" \
        --extra-cflags="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
        --extra-ldflags="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
        && make -j"$jobs" && make install)
}

build_freetype() {
    meson_build "$1" -Dbrotli=disabled -Dbzip2=disabled -Dharfbuzz=disabled -Dpng=disabled -Dzlib=system
}

build_fribidi() {
    meson_build "$1" -Ddocs=false -Dbin=false -Dtests=false
}

build_harfbuzz() {
    meson_build "$1" -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled \
        -Dicu=disabled -Dcoretext=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled
}

build_libass() {
    # Fonts through Core Text; no assembly for the same reason as ffmpeg.
    meson_build "$1" -Dfontconfig=disabled -Dcoretext=enabled -Ddirectwrite=disabled \
        -Dasm=disabled -Dlibunibreak=disabled -Dtest=disabled -Dcompare=disabled \
        -Dprofile=disabled -Dfuzz=disabled -Dcheckasm=disabled
}

# Header-only, both libplacebo's git submodules, which its tarball lacks.
# It compiles its Vulkan stubs even with Vulkan off.
build_vulkan_headers() {
    mkdir -p "$prefix/include"
    cp -R "$1/include/vulkan" "$1/include/vk_video" "$prefix/include/"
}

build_fast_float() {
    mkdir -p "$prefix/include"
    cp -R "$1/include/fast_float" "$prefix/include/"
}

build_libplacebo() {
    # mpv requires it, but with no video output there is nothing to render:
    # no GPU API at all, so no glad or Vulkan headers.
    meson_build "$1" -Dc_args="-I$prefix/include" -Dcpp_args="-I$prefix/include" -Dvulkan=disabled -Dopengl=disabled -Dd3d11=disabled -Ddemos=false \
        -Dtests=false -Dlcms=disabled -Dlibdovi=disabled -Dxxhash=disabled -Dshaderc=disabled \
        -Dglslang=disabled -Dunwind=disabled
}

build_mpv() {
    # libmpv only, LGPL, and of the optional features just Core Audio: the
    # app plays audio through it and nothing else. The one shared library.
    # Core Audio's device lookup calls osdep/utils-mac.c, which mpv builds
    # only with Cocoa, and Cocoa wants Swift and the video output: build the
    # one file with Core Audio instead.
    sed -i '' "s|'audio/out/ao_coreaudio_utils.c')|'audio/out/ao_coreaudio_utils.c', 'osdep/utils-mac.c')|" \
        "$1/meson.build"
    grep -q "'osdep/utils-mac.c')" "$1/meson.build"
    # libplacebo is C++, but its .pc doesn't say so, and mpv links as C.
    meson setup "$1/_build" "$1" --prefix="$prefix" --libdir=lib --buildtype=release \
        -Ddefault_library=shared -Dprefer_static=true -Dauto_features=disabled \
        -Dlibmpv=true -Dcplayer=false -Dgpl=false -Dtests=false -Dbuild-date=false \
        -Dgl=disabled -Dcoreaudio=enabled -Dzlib=enabled -Dc_link_args=-lc++
    meson compile -C "$1/_build"
    meson install -C "$1/_build"
    # meson names it by its absolute path; the app finds it through its rpath
    # (the build directory's link path, the bundle's Frameworks/).
    local lib=$prefix/lib/libmpv.2.dylib
    install_name_tool -id @rpath/libmpv.2.dylib "$lib"
    # A symbol none of its libraries defines links fine and is looked up only
    # when first called, so a missing one surfaces as a crash mid-playback.
    local missing
    missing=$(nm -mu "$lib" | grep "dynamically looked up" || true)
    if [[ -n $missing ]]; then
        echo "build-deps: libmpv has unresolved symbols:" >&2
        echo "$missing" >&2
        exit 1
    fi
}

mkdir -p "$cache" "$work" "$prefix"
for entry in "${sources[@]}"; do
    read -r name version url sha <<<"$entry"
    if [[ -f $(stamp "$name" "$version") ]]; then
        echo "build-deps: $name $version already built"
        continue
    fi
    echo "build-deps: building $name $version for $arch (macOS $MACOSX_DEPLOYMENT_TARGET+)"
    src=$(fetch "$name" "$version" "$url" "$sha")
    "build_$name" "$src"
    touch "$(stamp "$name" "$version")"
done
echo "build-deps: done, PKG_CONFIG_PATH=$prefix/lib/pkgconfig"
