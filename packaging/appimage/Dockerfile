# Build environment for cloudmus-qt's AppImage.
#
# Debian 11 "bullseye" (glibc 2.31, released 2021) is the deliberate
# choice here, not Debian 12 — see the plan/README in packaging/appimage/
# for the full reasoning: the AppImage's actual minimum-supported-distro
# floor is set by the glibc symbol versions our own compiled code (and
# anything linuxdeploy bundles) links against, and bullseye keeps that
# floor around 2021 ("~5 years old"). Debian 12 ships Qt6 directly via
# apt, which would be simpler, but its newer glibc (2.36) would raise
# that floor and likely break on real ~5-year-old distros.
#
# Bullseye has NO Qt6 packages at all (Qt6 only landed in Debian starting
# with bookworm) — Qt6 itself comes from aqtinstall below instead of apt,
# specifically to keep this old glibc floor while still getting a real
# Qt6.
#
# Layer order below is deliberately *not* top-to-bottom "logical" order —
# it's ordered slowest-and-least-likely-to-change first, so iterating on
# the (much more volatile, frequently tweaked while debugging) apt
# package list further down never invalidates the expensive Qt6/AppImage-
# tooling downloads above it. Docker only invalidates a layer's cache
# when *that* layer's own instruction changes or an *earlier* layer's
# cache was invalidated — never because a *later* layer changed.
#
# --- OpenSSL 3.x source stage ---
# Qt6's TLS backend plugin (from aqtinstall below) does a *runtime*
# dlopen("libssl.so.3") — not a link-time dependency linuxdeploy's own
# scanner would catch — and specifically needs OpenSSL 3.x symbols
# (EVP_PKEY_param_check, OPENSSL_sk_new_null, etc.); Debian 11 only
# packages OpenSSL 1.1.1, so without bundling our own 3.x, every HTTPS
# connection through Qt's network stack fails ("Incompatible version of
# OpenSSL (built with OpenSSL >= 3.x, runtime version is < 3.x)"),
# confirmed by watching a built AppImage fail exactly this way.
#
# A Debian 12 libssl3 .deb was tried first and rejected: it needs GLIBC
# up to 2.34, *newer* than bullseye's own 2.31 floor — bundling it would
# have made OpenSSL the new (worse) compatibility bottleneck instead of
# solving anything. The manylinux2014 build image (used upstream to
# build portable Python wheels) bundles its own OpenSSL 3.5 at
# /opt/_internal/openssl-3.5/lib/ instead, built against a much older
# glibc floor (2.17, confirmed via objdump -T) — comfortably under even
# Qt6's own requirement (2.28) — for exactly this "modern OpenSSL on an
# old glibc" use case. Only used to copy two .so files out of; nothing
# else from this image ends up in the final build environment.
FROM quay.io/pypa/manylinux2014_x86_64 AS openssl3-source

FROM debian:bullseye

ENV DEBIAN_FRONTEND=noninteractive

# Proxy passthrough: several hosts this build needs (download.qt.io,
# GitHub releases, PyPI, some Qt mirrors) can be region-blocked or
# throttled for a given network — see build-appimage.sh, which forwards
# HTTP_PROXY/HTTPS_PROXY/NO_PROXY (and lowercase) from the invoking
# shell's own environment as --build-arg automatically, if set. Declared
# here (not just relied on as Docker's own automatic proxy build-args,
# which only apply to the daemon's own image-pull/registry traffic) so
# every RUN below — apt, curl, pip, aqt — actually sees them as env vars.
# A no-op when unset.
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY
ARG http_proxy
ARG https_proxy
ARG no_proxy
ENV HTTP_PROXY=${HTTP_PROXY} \
    HTTPS_PROXY=${HTTPS_PROXY} \
    NO_PROXY=${NO_PROXY} \
    http_proxy=${http_proxy} \
    https_proxy=${https_proxy} \
    no_proxy=${no_proxy}

# Debian 11 is past its 5-year LTS window as of this writing — its normal
# mirrors (deb.debian.org) already 404 on bullseye-security. archive.
# debian.org keeps every EOL release's regular packages available
# indefinitely at a frozen snapshot, but does *not* mirror the security
# suite under the same bullseye-security path — drop that line entirely
# rather than chase its archive layout: this is a build toolchain image,
# not a deployed service, so late security-only point releases of apt
# packages don't matter here. Check-Valid-Until also has to be disabled,
# since an archived release's Release file carries a now-past expiry
# timestamp apt would otherwise refuse to trust.
RUN sed -i \
        -e 's|deb.debian.org/debian|archive.debian.org/debian|g' \
        -e '/-security/d' \
        /etc/apt/sources.list \
    && echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid-until

RUN apt-get update

# Minimal, stable set needed just to *fetch* Qt6 and the AppImage tools
# below — kept deliberately separate from the much larger build-toolchain
# package list further down, which is far more likely to need tweaking
# as new dependencies are discovered (as happened repeatedly while first
# getting this build working).
RUN apt-get install -y --no-install-recommends \
        python3 \
        python3-pip \
        python3-venv \
        ca-certificates \
        curl

# --- Qt6 (slow, large download, version pinned — rarely changes). The
# PyPI package is "aqtinstall", but the module/console script it installs
# is named "aqt", not "aqtinstall". No --modules flag: QtSvg (the one
# extra component cloudmus-qt needs beyond Core/Widgets/Network/DBus) is
# not a separate addon for 6.5.3 — it's already bundled into the base
# gcc_64 package's own archives (confirmed by inspecting Updates.xml
# directly: qtsvg-*.7z appears in the base package's
# <DownloadableArchives>, and there is no qt.qt6.653.addons.qtsvg entry
# at all). Passing --modules qtsvg errors out ("packages ['qtsvg'] were
# not found") since aqt has nothing separate to install.
#
# --base pins a specific mirror instead of letting aqt probe
# download.qt.io first and fall back automatically — download.qt.io can
# be unreachable/blocked on some networks, wasting minutes on a timeout
# before aqt even tries its own fallback. But no single hardcoded mirror
# is safe to assume either — mirrors.aliyun.com resolves and answers
# HTTP requests but doesn't mirror qtproject at all (404s the whole
# path), and mirrors.ustc.edu.cn (or a WAF in front of it) returns HTTP
# 403 specifically to Python's default `requests` User-Agent —
# aqtinstall's own HTTP client — while plain `curl` gets 200 from the
# exact same URL (that looked like flakiness at first: aqt burns a
# couple of minutes retrying before giving up on a mirror that's
# actually deterministically blocking it).
#
# So: probe a short list of candidates with `curl -f` (closest proxy for
# what aqt itself needs — a real Updates.xml, not just any 200) and use
# whichever answers first, rather than trusting one hardcoded guess.
# HTTP_PROXY above, if set, applies here too — on a network where
# certain hosts are blocked outright, that's the other half of getting
# any of this to succeed at all.
RUN QT_UPDATES_PATH="online/qtsdkrepository/linux_x64/desktop/qt6_653/Updates.xml"; \
    for mirror in \
        https://download.qt.io \
        https://mirrors.dotsrc.org/qtproject \
        https://ftp.jaist.ac.jp/pub/qtproject \
        https://qtproject.mirror.liquidtelecom.com \
        https://mirrors.tuna.tsinghua.edu.cn/qtproject \
        https://mirrors.ustc.edu.cn/qtproject \
    ; do \
        echo "Probing Qt mirror: ${mirror}" >&2; \
        if curl -fsS -m 15 -o /dev/null "${mirror}/${QT_UPDATES_PATH}"; then \
            echo "${mirror}" > /opt/qt-mirror.txt; \
            echo "Using Qt mirror: ${mirror}" >&2; \
            break; \
        fi; \
    done; \
    [ -s /opt/qt-mirror.txt ] || { echo "No working Qt mirror found" >&2; exit 1; }

# Split from the aqt install-qt call below into its own layer/RUN: this
# one is tiny and near-never fails, so caching it separately means a
# retry of the (much slower, occasionally-flaky) actual Qt download
# doesn't also redo this pip install every time.
RUN python3 -m pip install --no-cache-dir aqtinstall

ARG QT_VERSION=6.5.3
# --UNSAFE-ignore-hash: aqt validates each downloaded archive against a
# checksum it fetches from a *hardcoded* mirror list that always
# includes download.qt.io, regardless of --base — confirmed by watching
# it fail here with "Failed to download checksum ... from mirrors
# ['https://download.qt.io']" even with --base pointed elsewhere. On a
# network where download.qt.io itself is unreachable, that makes the
# checksum step itself fail and takes the whole install down with it, no
# matter how good the chosen --base mirror is. We're still fetching over
# HTTPS from a mirror step above already confirmed to serve the real Qt
# SDK tree, so skipping this extra verification is an acceptable
# tradeoff here, not a "trust some random host" shortcut.
RUN aqt install-qt linux desktop "${QT_VERSION}" gcc_64 \
        --outputdir /opt/qt \
        --base "$(cat /opt/qt-mirror.txt)" \
        --UNSAFE-ignore-hash
ENV Qt6_DIR=/opt/qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6
ENV PATH="/opt/qt/${QT_VERSION}/gcc_64/bin:${PATH}"

# --- Portable Python for the *bundled backend runtime* (not the build
# itself) — Debian 11's system python3 is 3.9, but all four backends'
# pyproject.toml declare `requires-python = ">=3.11"`, so pip refuses to
# install them into a venv built from it ("requires a different Python:
# 3.9.2 not in '>=3.11'"). Bullseye has no apt package for 3.11+ either.
# python-build-standalone's prebuilt CPython is the same kind of
# "portable official binary" story as aqtinstall's Qt above: this exact
# build is linked against glibc up to 2.17 only (checked via `objdump -T
# | grep GLIBC_`), comfortably under this image's 2.31 floor, so it runs
# here and on the AppImage's real target distros alike. Only used by
# build-in-docker.sh's `python3 -m venv` for the *bundled* runtime that
# ships inside the AppDir — unrelated to PYTHON3_EXECUTABLE/protocol
# codegen above, which still uses the container's own system python3. ---
ARG PYTHON_STANDALONE_VERSION=3.11.16+20260901
RUN mkdir -p /opt/python-standalone && \
    curl -fL "https://github.com/astral-sh/python-build-standalone/releases/download/${PYTHON_STANDALONE_VERSION#*+}/cpython-${PYTHON_STANDALONE_VERSION}-x86_64-unknown-linux-gnu-install_only.tar.gz" \
    | tar xz -C /opt/python-standalone --strip-components=1
ENV BUNDLED_PYTHON3=/opt/python-standalone/bin/python3.11

# Only the two .so files actually needed, out of the whole
# openssl3-source stage above.
RUN mkdir -p /opt/openssl3-bundle
COPY --from=openssl3-source \
    /opt/_internal/openssl-3.5/lib/libssl.so.3 \
    /opt/_internal/openssl-3.5/lib/libcrypto.so.3 \
    /opt/openssl3-bundle/

# --- AppImage packaging tools (moderate download, rarely changes), run
# later via each tool's own extracted squashfs-root/AppRun (Docker
# containers have no FUSE device by default; see packaging/appimage/
# build-in-docker.sh for why that's more reliable here than
# --appimage-extract-and-run). Deliberately NOT added to PATH: everything
# that needs one of these invokes its extracted AppRun by absolute path
# instead (see build-in-docker.sh) — and linuxdeploy's own plugin
# discovery does a naive, non-stopping prefix scan of every PATH
# directory for anything named `linuxdeploy-plugin-*`. With this
# directory on PATH, that scan also matches the raw, un-extracted
# linuxdeploy-plugin-qt.AppImage sitting right here (its name starts
# with the same prefix) and ends up preferring that broken match over
# the real extracted binary build-in-docker.sh puts on PATH instead —
# confirmed by watching it invoke this exact file directly and abort
# with exit code 127 (it can't run standalone without FUSE). ---
RUN mkdir -p /opt/appimage-tools && cd /opt/appimage-tools && \
    curl -fL -o linuxdeploy.AppImage \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && \
    curl -fL -o linuxdeploy-plugin-qt.AppImage \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage && \
    curl -fL -o appimagetool.AppImage \
        https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage && \
    chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage appimagetool.AppImage

# --- The rest of the build toolchain (also slow — several minutes — but
# now isolated so future tweaks here don't invalidate the Qt6/tooling
# layers above). ---
# The debian:bullseye base image itself was last refreshed against the
# live mirror before it went fully EOL, so a couple of core packages
# (libc6, perl-base) are already newer than what archive.debian.org's
# frozen snapshot pairs them with (dev packages like libc6-dev/perl
# depend on an *exact* matching version) — apt's solver doesn't reliably
# work this out on its own even with --allow-downgrades once it's mixed
# in with a large unrelated install list, so force just this narrow,
# well-defined downgrade first as its own step.
RUN apt-get install -y --allow-downgrades \
        libc6=2.31-13+deb11u11 \
        perl-base=5.32.1-4+deb11u3

# libxkbcommon-x11-0 and the libxcb-* runtime packages below are Qt's
# xcb platform plugin's own runtime deps — the -dev packages above
# (needed to link cloudmus-qt itself) don't pull all of them in as
# transitive dependencies. linuxdeploy's Qt plugin scans the *system*
# for these to bundle into the AppImage, and fails outright
# ("ERROR: Could not find dependency: libxkbcommon-x11.so.0") if even
# one is missing from the container, not just a warning.
RUN apt-get install -y --no-install-recommends \
        build-essential \
        ninja-build \
        pkg-config \
        git \
        libmpv-dev \
        libgl1-mesa-dev \
        libxkbcommon-dev \
        libxcb1-dev \
        libx11-dev \
        libxext-dev \
        libxrender-dev \
        libxi-dev \
        libxkbcommon-x11-0 \
        libxcb-cursor0 \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-randr0 \
        libxcb-render-util0 \
        libxcb-shape0 \
        libxcb-xinerama0 \
        libxcb-xkb1 \
        desktop-file-utils \
        patchelf \
        file \
        fuse3 \
    && rm -rf /var/lib/apt/lists/*

# Not from apt: bullseye's cmake package is 3.18.4, but
# fronts/qt/CMakeLists.txt requires 3.21+ (cmake_minimum_required).
# PyPI's cmake wheel ships a recent prebuilt binary (manylinux, so it
# runs fine against this old glibc) — simpler than adding Kitware's own
# apt repo just for this.
RUN python3 -m pip install --no-cache-dir "cmake>=3.21"

# Deliberately no COPY of the repo here — it's bind-mounted at `docker
# run` time by build-appimage.sh, so editing app code never invalidates
# any layer in this image.
WORKDIR /workspace
