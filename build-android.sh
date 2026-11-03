#!/bin/bash
# Builds the mobile front's APK (fronts/mobile) on this machine.
#
#   ./build-android.sh [abi ...]      default: arm64-v8a  (also armeabi-v7a, x86_64)
#
# Needs Qt 6.7.3 for Android and the desktop (kits under $QT_ROOT, setup in
# docs/android.md), an Android SDK with platform 34 and NDK r26b, and JDK 17.
# The APKs land in dist/cloudmus-mobile-<abi>.apk, signed with a dev key.
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
# project, a Maven AAR. Its header isn't in the AAR: the system's mpv/client.h.
mpv_version=0.4.1
mpv_dir=$work/libmpv-$mpv_version
if [ ! -f "$mpv_dir/.done" ]; then
    rm -rf "$mpv_dir" && mkdir -p "$mpv_dir"
    curl -fL -o "$work/libmpv.aar" \
        "https://repo1.maven.org/maven2/dev/jdtech/mpv/libmpv/$mpv_version/libmpv-$mpv_version.aar"
    unzip -q "$work/libmpv.aar" -d "$mpv_dir"
    mkdir -p "$mpv_dir/include/mpv"
    cp /usr/include/mpv/client.h /usr/include/mpv/render.h "$mpv_dir/include/mpv/" 2>/dev/null \
        || cp /usr/include/mpv/client.h "$mpv_dir/include/mpv/"
    touch "$mpv_dir/.done"
fi

# A debug key for the APKs: they're for sideloading.
keystore=$work/dev.keystore
if [ ! -f "$keystore" ]; then
    keytool -genkeypair -keystore "$keystore" -storepass android -keypass android -alias cloudmus-dev \
        -keyalg RSA -keysize 2048 -validity 36500 -dname "CN=CloudMus dev" > /dev/null
fi
export QT_ANDROID_KEYSTORE_PATH=$keystore QT_ANDROID_KEYSTORE_ALIAS=cloudmus-dev \
    QT_ANDROID_KEYSTORE_STORE_PASS=android QT_ANDROID_KEYSTORE_KEY_PASS=android

[ -f fronts/mobile/media/tracks.json ] || fronts/mobile/tools/fetch-dev-tracks.sh

for abi in "${abis[@]}"; do
    case $abi in
        arm64-v8a) kit=android_arm64_v8a ;;
        armeabi-v7a) kit=android_armv7 ;;
        x86_64) kit=android_x86_64 ;;
        *) echo "unknown ABI $abi" >&2; exit 1 ;;
    esac
    build=$work/$abi
    # aqtinstall leaves some kits' tools without the execute bit.
    [ -x "$QT_ROOT/$kit/bin/qt-cmake" ] || chmod +x "$QT_ROOT/$kit/bin/"*
    "$QT_ROOT/$kit/bin/qt-cmake" -S fronts/mobile -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DQT_HOST_PATH="$QT_ROOT/gcc_64" \
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
        -DQT_ANDROID_SIGN_APK=ON \
        -DCLOUDMUS_MPV_ANDROID_DIR="$mpv_dir"
    cmake --build "$build" --target apk
    apk=$(find "$build" -name '*.apk' -path '*release*' | head -1)
    [ -n "$apk" ] || { echo "no APK built for $abi" >&2; exit 1; }
    cp "$apk" "dist/cloudmus-mobile-$abi.apk"
    echo "dist/cloudmus-mobile-$abi.apk"
done
