#!/bin/bash
# Runs *inside* the packaging/appimage/Dockerfile container, with the
# repo bind-mounted at /workspace (see build-appimage.sh). Produces
# dist/CloudMus-x86_64.AppImage.
set -euo pipefail

REPO_ROOT="$(pwd)"
DIST_DIR="${REPO_ROOT}/dist"
APPDIR="${DIST_DIR}/AppDir"
APPIMAGE_TOOLS_DIR="/opt/appimage-tools"

# The bind-mounted repo is owned by the host user, not the container's
# root, so git refuses to touch it ("dubious ownership") and
# cmake/Version.cmake's `git describe` would silently fall back to the
# bare base version instead of the release tag.
git config --global --add safe.directory "${REPO_ROOT}"

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}" "${DIST_DIR}"

# --- 1. Protocol codegen (Python side only — the C++ side is
# regenerated automatically by CMake's own add_custom_command). Needed
# before `pip install`ing backends/py-rpc-common below, which imports
# rpc_common.generated.*. ---
echo "==> Generating Python protocol stubs"
pip3 install --no-cache-dir --quiet jinja2 pyyaml
python3 protocol/codegen/generate.py --lang python

# --- 2. Build cloudmus-qt ---
echo "==> Configuring and building cloudmus-qt"
# -DPYTHON3_EXECUTABLE forces CMakeLists.txt's own .venv-vs-system-python
# choice: the repo's checked-out .venv (the host developer's own local
# dev environment) is bind-mounted into this container along with
# everything else, so it "exists" here too, but it was built against the
# host's Python and can't see jinja2/pyyaml from inside this container —
# use the container's system python3 (which got them installed just
# above) instead.
cmake -S fronts/qt -B build-appimage -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPYTHON3_EXECUTABLE="$(command -v python3)"
cmake --build build-appimage

# --- 3. Assemble the AppDir ---
echo "==> Assembling AppDir"
mkdir -p "${APPDIR}/usr/bin"
cp build-appimage/bin/cloudmus-qt "${APPDIR}/usr/bin/cloudmus-qt"

# Bundled Python runtime: built from $BUNDLED_PYTHON3 (a portable
# CPython 3.11 from python-build-standalone, set up in the Dockerfile),
# not the container's system python3 (3.9) — all four backends declare
# `requires-python = ">=3.11"`, and Debian 11 has no apt package for
# that.
#
# Deliberately NOT `python3 -m venv --copies` here, despite that being
# the obvious/usual approach: a venv only copies the interpreter
# *binary* itself, not its standard library — it still resolves its
# stdlib by referring back to the *original* install's own lib/
# directory (via pyvenv.cfg's `home` key and CPython's own prefix
# landmark search), which is this build container's /opt/python-
# standalone. That path doesn't exist at all once shipped inside the
# AppImage, so every launch failed immediately with "Fatal Python
# error: init_fs_encoding ... ModuleNotFoundError: No module named
# 'encodings'" — confirmed by testing the built AppImage directly.
#
# Instead, copy python-build-standalone's *entire* installation tree
# (bin/ + lib/ together, stdlib included) into the AppDir and pip-
# install the backends straight into that copy's own site-packages —
# no venv layer at all. Verified this makes the copy fully
# self-locating with no extra env vars needed (it finds its own stdlib
# via the same landmark search, now correctly relative to wherever it
# actually is). No isolation from a "system" Python is lost by skipping
# the venv here either: this copy is already dedicated solely to these
# four backends, nothing else shares it.
echo "==> Bundling a Python runtime + backends"
cp -a "$(dirname "$(dirname "${BUNDLED_PYTHON3}")")" "${APPDIR}/usr/python-runtime"
"${APPDIR}/usr/python-runtime/bin/pip3" install --no-cache-dir \
    ./backends/py-rpc-common \
    ./backends/yandex-music \
    ./backends/local-folder \
    ./backends/youtube-music
ln -s ../python-runtime/bin/python3 "${APPDIR}/usr/bin/python3"

# No usr/bin/yt-dlp symlink here (unlike python3 above): mpv's ytdl_hook
# needs to find yt-dlp too (see packaging/appimage/AppRun's own, much
# longer comment on this), but AppRun ends up generating a small wrapper
# script for it at runtime instead of using pip's own console-script
# entry point directly — that file's shebang bakes in *this build
# container's* path, which doesn't exist wherever the AppImage actually
# ends up running.

# Desktop integration: .desktop + icon, under the standard FHS paths
# linuxdeploy actually scans (usr/share/applications, usr/share/icons) —
# NOT AppDir root. Root-level copies (plus AppRun) are what linuxdeploy
# itself *generates* from these during the run below ("WARNING: Could
# not find desktop file in AppDir" if placed at AppDir root instead).
# Our own AppRun comes *after* linuxdeploy runs, wrapping its generated
# one — see step 5.
mkdir -p "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/icons/hicolor/512x512/apps"
cp packaging/appimage/cloudmus-qt.desktop "${APPDIR}/usr/share/applications/cloudmus-qt.desktop"
cp art/logo.png "${APPDIR}/usr/share/icons/hicolor/512x512/apps/cloudmus-qt.png"

# Each backend's sidebar icon (manifest.json's "icon") — AppRun writes
# an absolute path to these into the manifests it seeds every launch.
mkdir -p "${APPDIR}/usr/share/cloudmus/backend-icons"
for id in local-folder yandex-music youtube-music; do
    cp "backends/${id}/icon.svg" "${APPDIR}/usr/share/cloudmus/backend-icons/${id}.svg"
done

# --- 4. Extract the packaging tools once (no FUSE in Docker — invoking
# each tool's own squashfs-root/AppRun directly is more reliable here
# than repeated --appimage-extract-and-run calls). ---
extract_tool() {
    local appimage="$1" dest="$2"
    if [ ! -d "${dest}" ]; then
        local work
        work="$(mktemp -d)"
        (cd "${work}" && "${appimage}" --appimage-extract >/dev/null)
        mv "${work}/squashfs-root" "${dest}"
        rmdir "${work}"
    fi
}
extract_tool "${APPIMAGE_TOOLS_DIR}/linuxdeploy.AppImage" "${APPIMAGE_TOOLS_DIR}/linuxdeploy"
extract_tool "${APPIMAGE_TOOLS_DIR}/linuxdeploy-plugin-qt.AppImage" "${APPIMAGE_TOOLS_DIR}/linuxdeploy-plugin-qt"
extract_tool "${APPIMAGE_TOOLS_DIR}/appimagetool.AppImage" "${APPIMAGE_TOOLS_DIR}/appimagetool"

# linuxdeploy discovers plugins by scanning PATH for files matching
# linuxdeploy-plugin-<name>. Must point at usr/bin specifically, not the
# squashfs-root dir itself: the actual executable lives at
# usr/bin/linuxdeploy-plugin-qt (AppRun is just a symlink to it) — the
# squashfs-root top level has no file with that exact name, so pointing
# PATH there let the scan fall through to the raw, un-extracted
# linuxdeploy-plugin-qt.AppImage sitting on PATH from the Dockerfile
# instead, which linuxdeploy then tried to exec directly and failed
# (needs FUSE, unavailable in Docker — see the comment on extract_tool
# above for why we extract these tools instead of running them as-is).
export PATH="${APPIMAGE_TOOLS_DIR}/linuxdeploy-plugin-qt/usr/bin:${PATH}"

# --- 5. Bundle Qt (plugin pass first, so it can register its own
# exclusions before the generic dependency scan), then the generic pass
# — pointed at *both* cloudmus-qt and the bundled python3, so libmpv's
# and the interpreter's own shared-library trees get pulled in too. ---
#
# Must go through the main `linuxdeploy --plugin qt`, not
# linuxdeploy-plugin-qt standalone: per its own README, standalone mode
# only detects Qt modules by scanning *already-deployed* libraries under
# AppDir/usr/lib — which is still empty at this point in the pipeline
# (the generic dependency scan that populates it runs *after* this, on
# purpose, so the Qt plugin can register its exclusions first). Only
# `linuxdeploy --plugin qt` does the implicit pre-scan of
# AppDir/usr/bin/cloudmus-qt (already copied there in step 3) that
# populates usr/lib enough for module detection to work at all — a
# direct standalone call silently found zero Qt modules and skipped
# platform-plugin bundling entirely (empty "Found Qt modules:" in the
# log, and no usr/plugins/ at all afterwards).
#
# EXTRA_PLATFORM_PLUGINS is this plugin's own documented env var for
# bundling a platform plugin it can't detect automatically: Qt loads
# wayland support dynamically at runtime, not via a link-time dependency
# of cloudmus-qt, so the automatic scan only ever finds xcb (X11) on its
# own — without this, only libqxcb.so ends up in the AppImage. This
# makes Qt's own runtime auto-detection (WAYLAND_DISPLAY set → prefer
# wayland, else fall back to xcb) have a native Wayland plugin to pick,
# not just X11 via XWayland.
#
# EXTRA_PLATFORM_PLUGINS alone is NOT enough for a properly working
# native Wayland session, though: it only bundles the platforms/
# category. Without EXTRA_QT_MODULES=waylandcompositor too, the separate
# wayland-graphics-integration-client plugin (hardware/EGL buffer
# integration) never gets bundled at all — confirmed both by its
# directory being entirely absent from usr/plugins/ without this flag,
# and by a built AppImage's own runtime log: "Failed to load client
# buffer integration: wayland-egl" / "Available client buffer
# integrations: QList()". Without it, Qt Wayland falls back to a
# software path that also loses compositor-provided cursor theming
# (reported as a generic/primitive cursor instead of the desktop's
# actual theme).
# librabbitmq (an optional libmpv/ffmpeg AMQP-protocol dependency,
# cloudmus-qt never actually uses it — all playback is local files or
# plain HTTP(S) URLs) pulls in Debian 11's OpenSSL *1.1*
# (libssl.so.1.1/libcrypto.so.1.1) alongside our own bundled OpenSSL
# *3.x* below, which is the root cause of Qt's TLS backend failing
# ("Incompatible version of OpenSSL (built with OpenSSL >= 3.x, runtime
# version is < 3.x)") even with our 3.x files present — some component
# eagerly loads 1.1 into the process before Qt's own lazy TLS plugin
# gets a chance to.
#
# Cannot simply exclude/remove it, though: libavformat.so.58 (part of
# libmpv's own dependency chain) has librabbitmq.so.4 as a genuine hard
# NEEDED entry with an eager (non-lazy) data relocation
# (`amqp_empty_bytes`) — confirmed by testing `patchelf --remove-needed`
# on it directly, which produced "undefined symbol: amqp_empty_bytes"
# immediately on load, not a deferred/harmless failure. Excluding
# librabbitmq.so.4 from the AppImage (tried first) broke the *whole*
# AppImage outright instead — linuxdeploy's generated AppRun.real bakes
# in a hard runtime requirement for every library it saw during these
# scans, so cloudmus-qt's own launch failed immediately with "error
# while loading shared libraries: librabbitmq.so.4: cannot open shared
# object file" — a strictly worse failure than the OpenSSL warning this
# was meant to fix. So: keep it bundled, accept both OpenSSL versions
# coexist, and solve the version-selection problem on the Qt/OpenSSL
# side instead (still being investigated).
echo "==> Running linuxdeploy (Qt plugin)"
EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so" \
EXTRA_QT_MODULES="waylandcompositor" \
    "${APPIMAGE_TOOLS_DIR}/linuxdeploy/AppRun" \
    --appdir "${APPDIR}" \
    --plugin qt

echo "==> Running linuxdeploy (generic dependency scan)"
"${APPIMAGE_TOOLS_DIR}/linuxdeploy/AppRun" \
    --appdir "${APPDIR}" \
    --executable "${APPDIR}/usr/bin/cloudmus-qt" \
    --executable "${APPDIR}/usr/python-runtime/bin/python3"

# Bundling OpenSSL 3.x by hand: linuxdeploy's dependency scanner only
# follows actual ELF NEEDED entries, and nothing in the AppImage links
# against libssl/libcrypto at link time — Qt's TLS backend plugin loads
# it dynamically at runtime instead, so linuxdeploy never bundles it on
# its own. See the Dockerfile's openssl3-source stage for where these
# two files come from and why (a manylinux2014-built OpenSSL 3.5, not a
# Debian 12 one — the latter needs a newer glibc than even this build
# environment provides).
cp /opt/openssl3-bundle/libssl.so.3 /opt/openssl3-bundle/libcrypto.so.3 "${APPDIR}/usr/lib/"

# The unversioned symlinks below are the actual fix for Qt failing to
# use these, not just an extra: this specific Qt build's TLS backend
# (qsslsocket_openssl_symbols.cpp) turns out not to have
# SHLIB_VERSION_NUMBER defined, which skips its normal "look for
# libssl.so.<version>" attempt entirely and goes straight to searching
# for the *unversioned* "libssl.so"/"libcrypto.so" instead (normally
# only present via a system's openssl "-dev" package, per Qt's own
# source comments) — confirmed via LD_DEBUG=libs on a built AppImage,
# which showed it searching for exactly "libssl.so"/"libcrypto.so", not
# "libssl.so.3"/"libcrypto.so.3". Without a matching name in
# usr/lib/, that search fell through past our bundled files entirely to
# whatever unrelated, potentially older/mismatched libssl.so/
# libcrypto.so happened to exist on the *host* system outside the
# AppImage (e.g. from its own openssl-devel package) — symbol
# resolution failures and the "Incompatible version of OpenSSL" warning
# followed from that wrong file, not from our bundled one, which was
# never actually reached.
ln -s libssl.so.3 "${APPDIR}/usr/lib/libssl.so"
ln -s libcrypto.so.3 "${APPDIR}/usr/lib/libcrypto.so"

# Bundling the GL dispatch libraries by hand, same reasoning as OpenSSL
# above: linuxdeploy has a built-in excludelist (not something this repo
# configures) that treats the whole GL/GLX/EGL family as "always present
# on any Linux desktop" and deliberately never bundles it, even though
# libQt6Gui.so.6, libQt6OpenGL.so.6, and the bundled libmpv.so.1 (from
# bullseye's libmpv-dev) all have real ELF NEEDED entries on it —
# confirmed via objdump -p, and confirmed *not* our own doing: this
# project's CMakeLists.txt requests no Qt6::OpenGL* component and no code
# under fronts/qt/src/ touches QOpenGL* at all; AudioPlayer.cpp configures
# mpv with vid=no (audio only), never mpv's GL render API either. That
# "always present" assumption fails on a minimal/headless install (no
# desktop Mesa stack pulled in), which is exactly what surfaced this as
# "libOpenGL.so.0: cannot open shared object file" on a bare Ubuntu VM.
#
# Only the thin vendor-neutral dispatch/API libraries below are bundled —
# never the Mesa vendor/DRI backend itself (libGLX_mesa.so.0,
# libEGL_mesa.so.0, DRI drivers, libdrm), which these dispatch libs
# dlopen() at runtime based on the *target* machine's actual GPU/kernel,
# and which is exactly why linuxdeploy's own excludelist treats this
# whole family as host-provided in the first place. Bundling only the
# dispatch shims is safe even on a machine with zero GPU acceleration:
# nothing in this app ever calls a GL function (see above), so these only
# need to be *loadable* to satisfy the ELF NEEDED at process startup —
# never actually invoked.
cp /usr/lib/x86_64-linux-gnu/libOpenGL.so.0 \
    /usr/lib/x86_64-linux-gnu/libGLX.so.0 \
    /usr/lib/x86_64-linux-gnu/libGLdispatch.so.0 \
    /usr/lib/x86_64-linux-gnu/libGL.so.1 \
    /usr/lib/x86_64-linux-gnu/libEGL.so.1 \
    /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 \
    /usr/lib/x86_64-linux-gnu/libGLESv1_CM.so.1 \
    "${APPDIR}/usr/lib/"

# Closing the rest of the dependency graph by hand, generically: bullseye's
# libmpv-dev (bundled above) itself directly needs ~40 shared libraries
# (confirmed via objdump -p on libmpv.so.1 — jack, sndio, the whole cdio
# family, smbclient, rubberband, SDL2, uchardet, lcms2, bluray, dvdnav,
# lua5.2, wayland client libs, xkbcommon, jpeg, archive, pulse, asound,
# the full ffmpeg family, on top of the GL family already handled above),
# and linuxdeploy's excludelist treats a surprising chunk of that as
# "presumed present on the host" too — confirmed in practice: after
# bundling only the GL libraries above, the next real-machine test
# (a bare Ubuntu VM) failed on "libjack.so.0: cannot open shared object
# file" next. Enumerating this one crash report at a time doesn't scale
# against a dependency graph this wide, so instead of a second static
# list, this closes the *whole* graph: scan every ELF already in the
# AppDir, bundle whatever it needs that isn't there yet and isn't on the
# deny-list below, and repeat until a full pass adds nothing new (a
# freshly-bundled library can itself need something not yet bundled).
#
# The deny-list is the same "host-provided" reasoning as the GL block
# above, generalized to the four kinds of things that must NOT come from
# this build:
#   - The base runtime (libc/libstdc++/libgcc_s/ld-linux and friends):
#     this whole build's low-glibc-floor strategy (see Dockerfile) is
#     built on running against whatever glibc the *target* machine
#     actually has, not shipping our own.
#   - X11 (libX11 and its extension libraries): linuxdeploy itself never
#     bundles these either, on the same "any Linux desktop has it"
#     assumption — left alone here for consistency, not re-litigated.
#   - GL/EGL/GLX/OpenGL/GLES (already bundled by name above, so excluded
#     here only to avoid a redundant second copy) plus the Mesa
#     vendor/DRI backend and hardware-specific libraries (libGLX_mesa,
#     libEGL_mesa, libglapi, libdrm, libgbm, libva*, libvdpau) — these
#     dlopen() a concrete GPU/kernel backend at runtime and must keep
#     coming from the target machine, exactly as explained in the GL
#     block above; this list just extends that same boundary to the
#     libraries beyond the five it names explicitly.
#   - Wayland client libraries (libwayland-client/cursor/egl/server) —
#     added after a real regression, not by original design: bullseye's
#     libwayland is 1.18, but Qt 6.9.3's official prebuilt
#     libQt6WaylandClient.so.6 needs wl_proxy_marshal_flags, only added
#     in libwayland 1.20. Bundling bullseye's old copy (which linuxdeploy
#     *does* do on its own, unlike GL/X11 — confirmed via objdump -p
#     showing it already sitting in usr/lib before this loop ever runs)
#     shadowed a real, new-enough libwayland-client the host already had,
#     turning a working Wayland session into "undefined symbol:
#     wl_proxy_marshal_flags" (confirmed via QT_DEBUG_PLUGINS=1) — a
#     regression from the Qt 6.5.3 -> 6.9.3 bump, since 6.5.3's older
#     Wayland client module apparently never needed that symbol. Same
#     underlying reasoning as X11: a windowing-system client library has
#     to match the actual session/compositor it's running under, which
#     only the host can guarantee — a frozen build-container snapshot
#     can't. The explicit rm below undoes linuxdeploy's own bundling of
#     these four files; the regex just stops this loop from adding them
#     straight back on the next pass.
#   - Fontconfig/Freetype (libfontconfig.so.1, libfreetype.so.6) — same
#     "reads host-specific state" reasoning as Wayland/X11 above, not a
#     GPU/kernel one this time: fontconfig parses the *host's* actual
#     /etc/fonts/*.conf XML files (real host paths, nothing inside this
#     AppImage), and bullseye's fontconfig is old enough to not understand
#     a newer host's XML schema (xsi:nil attributes, a <reset-dirs>
#     element — confirmed on an openSUSE Tumbleweed host, a rolling
#     release with a much newer fontconfig than 2021's bullseye),
#     producing a wall of "invalid attribute"/"invalid constant"/"unknown
#     element" warnings on every startup even though fontconfig itself
#     tolerates the unparseable parts and keeps going. Same fix as
#     Wayland: every real Linux desktop already has a working, host-
#     version-matched fontconfig (nothing renders text without one) —
#     bundling a frozen build-container snapshot only reintroduces the
#     version-skew problem Wayland already taught us to avoid here.
#
# Everything else this loop finds (jack, sndio, cdio, smbclient,
# rubberband, SDL2, uchardet, lcms2, bluray, dvdnav, lua5.2, xkbcommon,
# jpeg, archive, pulse, asound, ffmpeg) is an ordinary userspace library
# with no host-specific state — same "only needs to be loadable, not
# necessarily exercised" reasoning as OpenSSL and the GL dispatch shims
# above.
rm -f "${APPDIR}"/usr/lib/libwayland-client.so* \
    "${APPDIR}"/usr/lib/libwayland-cursor.so* \
    "${APPDIR}"/usr/lib/libwayland-egl.so* \
    "${APPDIR}"/usr/lib/libwayland-server.so* \
    "${APPDIR}"/usr/lib/libfontconfig.so* \
    "${APPDIR}"/usr/lib/libfreetype.so*

HOST_PROVIDED_LIB_REGEX='^lib(c|m|dl|pthread|rt|resolv|nsl|util|anl)\.so|^libstdc\+\+\.so|^libgcc_s\.so|^ld-linux|^libX|^libEGL\.so|^libGL\.so|^libGLX\.so|^libGLdispatch\.so|^libOpenGL\.so|^libGLES|^libglapi\.so|^libdrm|^libgbm\.so|^libva|^libvdpau\.so|^libwayland|^libfontconfig\.so|^libfreetype\.so'
for _pass in 1 2 3 4 5; do
    added=0
    while IFS= read -r -d '' _elf; do
        while read -r _lib; do
            [ -n "$_lib" ] || continue
            [[ "$_lib" =~ $HOST_PROVIDED_LIB_REGEX ]] && continue
            [ -e "${APPDIR}/usr/lib/${_lib}" ] && continue
            _src=$(find /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu -maxdepth 1 -name "$_lib" 2>/dev/null | head -n1)
            if [ -n "$_src" ]; then
                cp "$_src" "${APPDIR}/usr/lib/${_lib}"
                added=1
            fi
        done < <(ldd "$_elf" 2>/dev/null | awk '{print $1}')
    done < <(find "${APPDIR}/usr/lib" "${APPDIR}/usr/bin" -type f \( -name '*.so*' -o -perm -u+x \) -print0)
    echo "==> Dependency closure pass ${_pass}: added ${added} new librar$([ "$added" = 1 ] && echo y || echo ies)"
    [ "$added" -eq 0 ] && break
done

# Blanket RPATH patch over every top-level library in usr/lib, regardless
# of which step put it there (linuxdeploy's own bundling, the manual
# OpenSSL/GL blocks above, or the closure loop just above): confirmed
# empirically that a file linuxdeploy bundled *itself*
# (libwayland-client.so.0, needed by Qt's Wayland platform integration)
# ships with no RPATH/RUNPATH at all, so its own NEEDED libffi.so.7 could
# only be found via the *target machine's* default search paths — not
# this AppDir — even though libffi.so.7 sits right next to it, bundled,
# in this very directory. DT_RUNPATH is deliberately not inherited by a
# library's own further dependencies (unlike the legacy DT_RPATH), so
# patching only the top-level executable (which linuxdeploy does do
# correctly — cloudmus-qt's own RUNPATH is $ORIGIN/../lib) was never
# going to be enough once *any* bundled library has further un-bundled-
# looking dependencies of its own. Cheaper and more robust than tracking
# down which specific files need it: just make every file in usr/lib able
# to find every one of its siblings, unconditionally.
find "${APPDIR}/usr/lib" -maxdepth 1 -type f -name '*.so*' -print0 | \
    xargs -0 -I{} patchelf --set-rpath '$ORIGIN' {}

# --- 6. Wrap linuxdeploy's generated AppRun (it correctly sets up
# QT_PLUGIN_PATH and friends for the bundled Qt6 — not something worth
# reimplementing by hand) with ours, which seeds backend manifests
# *before* handing off to it. See packaging/appimage/AppRun's own
# comments for why manifest-seeding can't just be done once at build
# time (an AppImage's $APPDIR is a fresh mount path every launch). ---
mv "${APPDIR}/AppRun" "${APPDIR}/AppRun.real"
cp packaging/appimage/AppRun "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"

# --- 7. Package the final AppImage ---
echo "==> Running appimagetool"
"${APPIMAGE_TOOLS_DIR}/appimagetool/AppRun" \
    "${APPDIR}" "${DIST_DIR}/CloudMus-x86_64.AppImage"

echo "==> Built: dist/CloudMus-x86_64.AppImage"
