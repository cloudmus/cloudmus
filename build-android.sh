#!/bin/bash
# Builds the mobile front's APK (fronts/mobile) on this machine.
#
#   ./build-android.sh [abi ...]      default: arm64-v8a  (also x86_64)
#
# Needs Qt 6.7.3 for Android and the desktop (kits under $QT_ROOT, setup in
# docs/android.md), an Android SDK with platform 34 and NDK r26b, and JDK 17.
# The APKs land in dist/CloudMus-<version>-<abi>.apk (<version> as in the other
# platforms' builds: `git describe`, so 0.6.5 on a release tag, 0.6.5-12-gabc1234
# between them), signed with a dev key, or with
# the one in CLOUDMUS_ANDROID_KEYSTORE (with CLOUDMUS_ANDROID_KEY_ALIAS and
# CLOUDMUS_ANDROID_KEYSTORE_PASSWORD), as CI does.
#
# Beside each, CloudMus-<version>-<abi>-signin.apk: CloudMus Sign-in
# (fronts/mobile/signin-helper), the helper a device with an old WebView signs
# in with, signed with the same key. It needs the SDK's platform 37.2.
# CLOUDMUS_ANDROID_HELPER=0 leaves it out.
#
# CLOUDMUS_ANDROID_DISTRIBUTION=store builds for Google Play and RuStore
# (fronts/mobile/CMakeLists.txt): updated by the store, no installing of its
# own; the APK is then CloudMus-<version>-<abi>-store.apk.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
root=$PWD

QT_ROOT=${QT_ROOT:-$HOME/Qt/6.7.3}
ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}
ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT_QT:-$ANDROID_SDK_ROOT/ndk/26.1.10909125}
JAVA_HOME=${CLOUDMUS_JAVA17_HOME:-$HOME/.sdkman/candidates/java/17.0.19-amzn}

work=$root/build-android
mkdir -p "$work" dist

# Qt 6.7 and its Gradle (AGP 7.4) build against the newest platform and
# build tools they find, which are far newer than they know: a view of the
# SDK with just the ones they were made for (all links, nothing copied).
full_sdk=$ANDROID_SDK_ROOT
sdk=$work/sdk
if [ ! -d "$sdk/platforms/android-34" ]; then
    rm -rf "$sdk" && mkdir -p "$sdk/platforms" "$sdk/build-tools"
    ln -s "$ANDROID_SDK_ROOT/platforms/android-34" "$sdk/platforms/android-34"
    ln -s "$ANDROID_SDK_ROOT/build-tools/35.0.0" "$sdk/build-tools/35.0.0"
    for entry in ndk platform-tools cmdline-tools licenses; do
        [ -e "$ANDROID_SDK_ROOT/$entry" ] && ln -s "$ANDROID_SDK_ROOT/$entry" "$sdk/$entry"
    done
fi
ANDROID_SDK_ROOT=$sdk
export ANDROID_SDK_ROOT ANDROID_NDK_ROOT JAVA_HOME
export ANDROID_HOME=$ANDROID_SDK_ROOT
export PATH=$JAVA_HOME/bin:$PATH

version="$(git describe --tags --dirty --match '[0-9]*' 2>/dev/null || true)"
if [ -z "$version" ]; then
    commit="$(git describe --always --dirty=.dirty 2>/dev/null || true)"
    version="0.1.0${commit:+\+g${commit}}"
fi

distribution=${CLOUDMUS_ANDROID_DISTRIBUTION:-github}
case $distribution in
    github) suffix="" ;;
    store) suffix="-store" ;;
    *) echo "CLOUDMUS_ANDROID_DISTRIBUTION is github or store" >&2; exit 1 ;;
esac

abis=("$@")
[ ${#abis[@]} -gt 0 ] || abis=(arm64-v8a)

# Gradle ignores the proxy environment variables: hand it the same proxy.
gradle_opts=""
for scheme in http https; do
    var="${scheme}_proxy"
    url=${!var:-}
    [ -n "$url" ] || continue
    read -r host port user pass < <(python3 - "$url" <<'PY'
import sys
from urllib.parse import urlparse
u = urlparse(sys.argv[1])
print(u.hostname or "", u.port or "", u.username or "-", u.password or "-")
PY
)
    gradle_opts+=" -D${scheme}.proxyHost=$host -D${scheme}.proxyPort=$port"
    if [ "$user" != "-" ]; then
        gradle_opts+=" -D${scheme}.proxyUser=$user -D${scheme}.proxyPassword=$pass"
    fi
done
# Java refuses Basic authentication to a proxy for https tunnels unless told
# otherwise — in the wrapper and in the daemon alike.
if [ -n "$gradle_opts" ]; then
    gradle_opts+=" -Djdk.http.auth.tunneling.disabledSchemes="
    gradle_opts+=" -Dorg.gradle.workers.max=2 -Dorg.gradle.jvmargs=-Xmx2g\\ -Djdk.http.auth.tunneling.disabledSchemes="
fi
export GRADLE_OPTS="${GRADLE_OPTS:-}${gradle_opts}"

# libmpv for Android: the prebuilt one (mpv 0.39, FFmpeg) of the libmpv-android
# project, a Maven AAR. Its headers aren't in the AAR: mpv's own, of that version.
mpv_version=0.4.1
mpv_headers=v0.39.0
mpv_dir=$work/libmpv-$mpv_version
if [ ! -f "$mpv_dir/.done" ]; then
    rm -rf "$mpv_dir" && mkdir -p "$mpv_dir"
    curl -fL --retry 5 --retry-all-errors --retry-delay 5 -o "$work/libmpv.aar" \
        "https://repo1.maven.org/maven2/dev/jdtech/mpv/libmpv/$mpv_version/libmpv-$mpv_version.aar"
    unzip -q "$work/libmpv.aar" -d "$mpv_dir"
    mkdir -p "$mpv_dir/include/mpv"
    for header in client.h render.h stream_cb.h; do
        curl -fL --retry 5 --retry-all-errors --retry-delay 5 -o "$mpv_dir/include/mpv/$header" \
            "https://raw.githubusercontent.com/mpv-player/mpv/$mpv_headers/libmpv/$header"
    done
    touch "$mpv_dir/.done"
fi

# Existing build directories may predate the stream callback header.
if [ ! -f "$mpv_dir/include/mpv/stream_cb.h" ]; then
    curl -fL --retry 5 --retry-all-errors --retry-delay 5 -o "$mpv_dir/include/mpv/stream_cb.h" \
        "https://raw.githubusercontent.com/mpv-player/mpv/$mpv_headers/libmpv/stream_cb.h"
fi

# The key an update has to come with: Android turns down another one. A debug
# key made here otherwise: those APKs are for sideloading from this machine.
if [ -n "${CLOUDMUS_ANDROID_KEYSTORE:-}" ]; then
    export QT_ANDROID_KEYSTORE_PATH=$CLOUDMUS_ANDROID_KEYSTORE
    export QT_ANDROID_KEYSTORE_ALIAS=$CLOUDMUS_ANDROID_KEY_ALIAS
    export QT_ANDROID_KEYSTORE_STORE_PASS=$CLOUDMUS_ANDROID_KEYSTORE_PASSWORD
    export QT_ANDROID_KEYSTORE_KEY_PASS=$CLOUDMUS_ANDROID_KEYSTORE_PASSWORD
else
    keystore=$work/dev.keystore
    if [ ! -f "$keystore" ]; then
        keytool -genkeypair -keystore "$keystore" -storepass android -keypass android -alias cloudmus-dev \
            -keyalg RSA -keysize 2048 -validity 36500 -dname "CN=CloudMus dev" > /dev/null
    fi
    export QT_ANDROID_KEYSTORE_PATH=$keystore QT_ANDROID_KEYSTORE_ALIAS=cloudmus-dev \
        QT_ANDROID_KEYSTORE_STORE_PASS=android QT_ANDROID_KEYSTORE_KEY_PASS=android
fi

for abi in "${abis[@]}"; do
    case $abi in
        arm64-v8a) kit=android_arm64_v8a; rust_target=aarch64-linux-android ;;
        armeabi-v7a) echo "no CPython for 32-bit units, so no sources there" >&2; exit 1 ;;
        x86_64) kit=android_x86_64; rust_target=x86_64-linux-android ;;
        *) echo "unknown ABI $abi" >&2; exit 1 ;;
    esac
    build=$work/$abi$suffix
    case "$(uname -s)" in
        Linux) ndk_host=linux-x86_64 ;;
        Darwin) ndk_host=darwin-x86_64 ;;
        *) echo "Unsupported Rust Android build host" >&2; exit 1 ;;
    esac
    rust_linker="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$ndk_host/bin/$rust_target"26-clang
    rust_linker_env="CARGO_TARGET_$(printf '%s' "$rust_target" | tr '[:lower:]-' '[:upper:]_')_LINKER"
    env "$rust_linker_env=$rust_linker" cargo build --release --locked --target "$rust_target" \
        --manifest-path backends/local-folder/Cargo.toml --target-dir "$work/rust"
    native_backend="$work/rust/$rust_target/release/cloudmus-backend-local-folder"
    # CPython and the backends.
    fronts/mobile/tools/bundle-python.sh "$abi" "$work/python-$abi"
    python_args=(-DCLOUDMUS_PYTHON_ANDROID_DIR="$work/python-$abi")
    rm -rf "$build/android-package/assets/media" # from before the real core
    # aqtinstall leaves some kits' tools without the execute bit.
    [ -x "$QT_ROOT/$kit/bin/qt-cmake" ] || chmod +x "$QT_ROOT/$kit/bin/"*
    "$QT_ROOT/$kit/bin/qt-cmake" -S fronts/mobile -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DQT_HOST_PATH="$QT_ROOT/gcc_64" \
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
        -DQT_ANDROID_SIGN_APK=ON -DCLOUDMUS_ANDROID_DISTRIBUTION="$distribution" \
        -DCLOUDMUS_MPV_ANDROID_DIR="$mpv_dir" -DCLOUDMUS_LOCAL_ANDROID_BINARY="$native_backend" "${python_args[@]}"
    cmake --build "$build" --target apk
    apk=$(find "$build" -name '*.apk' -path '*release*' | head -1)
    [ -n "$apk" ] || { echo "no APK built for $abi" >&2; exit 1; }
    cp "$apk" "dist/CloudMus-$version-$abi$suffix.apk"
    echo "dist/CloudMus-$version-$abi$suffix.apk"
    if [ "$distribution" = github ] && [ "${CLOUDMUS_ANDROID_HELPER:-1}" != 0 ]; then
        # Its own, newer Gradle, with the whole SDK; --no-daemon: not the one
        # the main app's build leaves behind.
        env ANDROID_HOME="$full_sdk" ANDROID_SDK_ROOT="$full_sdk" fronts/mobile/signin-helper/gradlew \
            -p fronts/mobile/signin-helper --no-daemon -q assembleRelease \
            -Pabi="$abi" -Pversion.name="$version"
        cp fronts/mobile/signin-helper/build/outputs/apk/release/cloudmus-signin-release.apk \
            "dist/CloudMus-$version-$abi-signin.apk"
        echo "dist/CloudMus-$version-$abi-signin.apk"
    fi
done
