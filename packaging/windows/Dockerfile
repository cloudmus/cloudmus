FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    WINEARCH=win64 \
    WINEPREFIX=/opt/cloudmus-wine \
    WINEDEBUG=-all

ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY
ARG http_proxy
ARG https_proxy
ARG no_proxy

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git python3 python3-venv python3-pip \
        p7zip-full unzip wine wine64 xvfb xauth nsis file \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/build-python && \
    /opt/build-python/bin/pip install --no-cache-dir \
        aqtinstall==3.3.0 jinja2 pyyaml build wheel
ENV PATH=/opt/build-python/bin:$PATH

# The Windows Qt tools (moc, windeployqt) and compiler run under Wine,
# alongside Windows CMake and Ninja. This keeps one ABI throughout.
ARG QT_VERSION=6.9.3
RUN aqt install-qt windows desktop "${QT_VERSION}" win64_mingw --outputdir /opt/qt
RUN aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir /opt/qt

ARG CMAKE_VERSION=3.31.6
RUN curl -fL --retry 3 -o /tmp/cmake.zip \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-windows-x86_64.zip" \
    && unzip -q /tmp/cmake.zip -d /opt \
    && mv "/opt/cmake-${CMAKE_VERSION}-windows-x86_64" /opt/windows-cmake \
    && rm /tmp/cmake.zip

ARG NINJA_VERSION=1.12.1
RUN mkdir -p /opt/windows-ninja && \
    curl -fL --retry 3 -o /tmp/ninja.zip \
        "https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-win.zip" \
    && unzip -q /tmp/ninja.zip -d /opt/windows-ninja \
    && rm /tmp/ninja.zip

# This archive has the headers, MinGW import library and standard x64 DLL.
# It is the non-v3 Windows build linked from mpv.io/installation/.
ARG MPV_ARCHIVE=mpv-dev-x86_64-20260928-git-e470f8986e.7z
ARG MPV_SHA256=81795d759e01016f1550fd71651a1a5d59ab5c28ef31c0b6793224e9cff39459
RUN curl -fL --retry 3 -o /tmp/mpv.7z \
        "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20260928/${MPV_ARCHIVE}" \
    && echo "${MPV_SHA256}  /tmp/mpv.7z" | sha256sum -c - \
    && mkdir -p /opt/mpv/lib \
    && 7z x -y /tmp/mpv.7z -o/opt/mpv >/dev/null \
    && mv /opt/mpv/libmpv.dll.a /opt/mpv/lib/ \
    && rm /tmp/mpv.7z

# libmpv imports vulkan-1.dll directly, and Windows ships it only with a
# GPU driver — without one (a VM, say) libmpv doesn't load at all. The
# Khronos loader alone is enough: with no Vulkan driver it just finds no
# devices, and audio playback never needs one.
ARG VULKAN_RT_VERSION=1.4.357.0
ARG VULKAN_RT_SHA256=a14672efed15aafc7f5a16572d35cd3a3416eadf670aeee3cdf50ee32d5fbf83
RUN curl -fL --retry 3 -o /tmp/vulkan-rt.zip \
        "https://sdk.lunarg.com/sdk/download/${VULKAN_RT_VERSION}/windows/VulkanRT-X64-${VULKAN_RT_VERSION}-Components.zip" \
    && echo "${VULKAN_RT_SHA256}  /tmp/vulkan-rt.zip" | sha256sum -c - \
    && mkdir -p /opt/vulkan-rt \
    && unzip -j -q /tmp/vulkan-rt.zip '*/x64/vulkan-1.dll' '*/VulkanRT-License.txt' -d /opt/vulkan-rt \
    && rm /tmp/vulkan-rt.zip

ARG PYTHON_VERSION=3.13.7
RUN curl -fL --retry 3 -o /opt/python-embed.zip \
    "https://www.python.org/ftp/python/${PYTHON_VERSION}/python-${PYTHON_VERSION}-embed-amd64.zip"

RUN mkdir -p "${WINEPREFIX}" && xvfb-run -a wineboot --init && wineserver -w

# The installer's banners, rendered from SVG (make-installer-bitmaps.sh).
# Last, so adding it didn't invalidate the layers above.
RUN apt-get update && apt-get install -y --no-install-recommends \
        librsvg2-bin imagemagick \
    && rm -rf /var/lib/apt/lists/*
