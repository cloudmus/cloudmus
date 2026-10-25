cmake_minimum_required(VERSION 3.21)
project(cloudmus-qt VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX) # AppKit calls in *Mac.mm
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Build speed. ccache and a faster linker are used when they are
# installed (none is required), and the Qt headers are precompiled; each has
# an option to switch it off. Set before any target exists: the launcher and
# the linker type are read when a target is created.
option(CLOUDMUS_CCACHE "Compile through ccache when it is installed" ON)
if(CLOUDMUS_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CLOUDMUS_CCACHE_PROGRAM ccache)
    if(CLOUDMUS_CCACHE_PROGRAM)
        # Precompiled headers are only cacheable with these; an explicit
        # CCACHE_SLOPPINESS of the user wins.
        set(_launcher ${CLOUDMUS_CCACHE_PROGRAM})
        if(NOT DEFINED ENV{CCACHE_SLOPPINESS})
            set(_launcher ${CMAKE_COMMAND} -E env CCACHE_SLOPPINESS=pch_defines,time_macros
                ${CLOUDMUS_CCACHE_PROGRAM})
        endif()
        set(CMAKE_C_COMPILER_LAUNCHER ${_launcher})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${_launcher})
        message(STATUS "Build speed: compiling through ${CLOUDMUS_CCACHE_PROGRAM}")
    endif()
endif()

option(CLOUDMUS_FAST_LINKER "Link with mold or lld when one is installed" ON)
if(CLOUDMUS_FAST_LINKER AND UNIX AND NOT APPLE)
    find_program(CLOUDMUS_MOLD_PROGRAM mold)
    find_program(CLOUDMUS_LLD_PROGRAM NAMES ld.lld lld)
    if(CLOUDMUS_MOLD_PROGRAM)
        set(_fast_linker MOLD)
    elseif(CLOUDMUS_LLD_PROGRAM)
        set(_fast_linker LLD)
    endif()
    if(_fast_linker)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
            set(CMAKE_LINKER_TYPE ${_fast_linker})
        else()
            string(TOLOWER ${_fast_linker} _fast_linker_name)
            add_link_options(-fuse-ld=${_fast_linker_name})
        endif()
        message(STATUS "Build speed: linking with ${_fast_linker}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg LinguistTools)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

if(WIN32)
    set(CLOUDMUS_MPV_ROOT "" CACHE PATH "Directory containing Windows libmpv include/ and lib/")
    find_path(MPV_INCLUDE_DIR mpv/client.h PATHS "${CLOUDMUS_MPV_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    find_library(MPV_IMPORT_LIBRARY NAMES mpv libmpv PATHS "${CLOUDMUS_MPV_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    add_library(cloudmus-mpv INTERFACE)
    target_include_directories(cloudmus-mpv INTERFACE "${MPV_INCLUDE_DIR}")
    target_link_libraries(cloudmus-mpv INTERFACE "${MPV_IMPORT_LIBRARY}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    add_library(cloudmus-mpv INTERFACE)
    target_link_libraries(cloudmus-mpv INTERFACE PkgConfig::MPV)
endif()

# --- protocol/codegen: regenerate fronts/qt/generated/ whenever the schema
# or methods change, before anything tries to #include it. Requires Python +
# jinja2/pyyaml at configure/build time — see protocol/README.md and the
# plan's "Generated code: gitignored" note for why this tradeoff was made.
# Prefer the repo's own .venv (where the rest of the project already
# installs jinja2/pyyaml, see backends/py-rpc-common) over a bare system
# python3 that likely doesn't have them — but only as a default: guarded
# by `if(NOT PYTHON3_EXECUTABLE)` so an explicit `-DPYTHON3_EXECUTABLE=`
# on the cmake command line wins instead. Needed for the AppImage Docker
# build (see packaging/appimage/build-in-docker.sh): the host repo's own
# .venv gets bind-mounted into the container along with everything else,
# so `EXISTS .venv/bin/python3` is still true in there, but that venv was
# created against the host's Python and doesn't resolve its packages
# correctly under the container's different Python/filesystem — it
# "exists" but can't actually see jinja2, unlike the container's own
# system python3 which build-in-docker.sh installs jinja2/pyyaml into.
set(CLOUDMUS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(CLOUDMUS_REPO_ROOT ${CLOUDMUS_REPO_ROOT} ABSOLUTE)

option(CLOUDMUS_PREGENERATED_PROTOCOL "Use generated protocol sources already present in the checkout" OFF)
# Python is only needed to generate the protocol sources; a build that uses
# the pregenerated ones (the Windows one, under Wine's CMake with no Python
# at all) must not require it.
if(NOT CLOUDMUS_PREGENERATED_PROTOCOL)
    if(NOT PYTHON3_EXECUTABLE)
        if(EXISTS ${CLOUDMUS_REPO_ROOT}/.venv/bin/python3)
            set(PYTHON3_EXECUTABLE ${CLOUDMUS_REPO_ROOT}/.venv/bin/python3)
        else()
            find_program(PYTHON3_EXECUTABLE NAMES python3 python REQUIRED)
        endif()
    endif()
endif()
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set(GENERATED_CPP_STAMP ${CMAKE_CURRENT_BINARY_DIR}/generated_cpp.stamp)

file(GLOB_RECURSE PROTOCOL_SCHEMA_FILES ${CLOUDMUS_REPO_ROOT}/protocol/schema/*.yaml)

# The generated sources are listed as outputs, not just the stamp: on a
# fresh checkout they don't exist yet (gitignored), and CMake refuses to
# configure with a missing source file unless it knows a rule produces it.
if(NOT CLOUDMUS_PREGENERATED_PROTOCOL)
add_custom_command(
    OUTPUT ${GENERATED_CPP_STAMP}
           ${GENERATED_DIR}/Models.h
           ${GENERATED_DIR}/RpcMethods.h
           ${GENERATED_DIR}/RpcMethods.cpp
    COMMAND ${PYTHON3_EXECUTABLE} ${CLOUDMUS_REPO_ROOT}/protocol/codegen/generate.py --lang cpp
    COMMAND ${CMAKE_COMMAND} -E touch ${GENERATED_CPP_STAMP}
    WORKING_DIRECTORY ${CLOUDMUS_REPO_ROOT}
    DEPENDS ${CLOUDMUS_REPO_ROOT}/protocol/methods.yaml
            ${PROTOCOL_SCHEMA_FILES}
            ${CLOUDMUS_REPO_ROOT}/protocol/codegen/generate.py
            ${CLOUDMUS_REPO_ROOT}/protocol/codegen/schema_ir.py
            ${CLOUDMUS_REPO_ROOT}/protocol/codegen/cpprender.py
    COMMENT "Regenerating fronts/qt/generated/ from protocol/"
    VERBATIM
)
add_custom_target(protocol_codegen_cpp DEPENDS ${GENERATED_CPP_STAMP})
else()
    foreach(source Models.h RpcMethods.h RpcMethods.cpp)
        if(NOT EXISTS "${GENERATED_DIR}/${source}")
            message(FATAL_ERROR "Missing pregenerated protocol file: ${GENERATED_DIR}/${source}")
        endif()
    endforeach()
    add_custom_target(protocol_codegen_cpp)
endif()

# --- cloudmus-core: everything below the view — backends, playback, state,
# settings — with no Qt Widgets, so the linker keeps widget code out of it
# (see fronts/qt/AGENTS.md). Linked by the app and by the tests.
add_library(cloudmus-core STATIC
    src/Rpc/NdjsonTransport.cpp
    src/Rpc/BackendManifest.cpp
    src/Rpc/RpcClient.cpp
    src/Rpc/SourceManager.cpp
    src/Rpc/AuthStates.cpp

    src/Playback/AudioPlayer.cpp
    src/Playback/AudioFileProbe.cpp
    src/Playback/StreamRelay.cpp
    src/Playback/NetworkPool.cpp
    src/Playback/StreamDownload.cpp
    src/Playback/TrackCache.cpp
    src/Playback/Preloader.cpp
    src/Playback/PlaybackController.cpp
    src/Playback/BeatDetector.cpp
    src/Playback/LevelFeed.cpp
    src/Playback/CuePlayer.cpp

    src/Config/Paths.cpp
    src/Config/Settings.cpp
    src/I18n/Languages.cpp
    src/I18n/Translator.cpp
    src/Net/ProxyRouting.cpp
    src/Net/NetworkErrors.cpp
    src/Net/ParallelDownload.cpp

    src/History/PlaybackHistory.cpp
    src/Library/TrackStates.cpp
    src/Library/DownloadPaths.cpp
    src/Library/TrackFile.cpp

    src/Covers/CoverArtCache.cpp

    src/App/Core.cpp
    src/App/Analytics.cpp
    src/App/SourceSession.cpp
    src/App/PlaylistEditing.cpp
    src/App/TrackFetch.cpp
    src/App/StarPrompt.cpp
    src/App/ListeningTime.cpp

    src/Hotkeys/Actions.cpp
    src/Hotkeys/Registry.cpp
    src/Hotkeys/Dispatcher.cpp

    src/ViewModel/AudioPulse.cpp
    src/ViewModel/Messages.cpp
    src/ViewModel/NowPlaying.cpp
    src/ViewModel/SidebarModel.cpp
    src/ViewModel/Sources.cpp
    src/ViewModel/ActivePlaylist.cpp
    src/ViewModel/Browse.cpp
    src/ViewModel/SourcePage.cpp
    src/ViewModel/Downloads.cpp
    src/ViewModel/Preload.cpp

    src/Diagnostics/CrashReporter.cpp
    src/Diagnostics/CrashShortcut.cpp

    src/Update/ReleaseFeed.cpp
    src/Update/UpdateChecker.cpp
    src/Update/UpdateDownloader.cpp
    src/Timing/Debounce.cpp
    src/Timing/Throttle.cpp

    ${GENERATED_DIR}/RpcMethods.cpp
)
add_dependencies(cloudmus-core protocol_codegen_cpp)
if(WIN32)
    target_sources(cloudmus-core PRIVATE src/Diagnostics/CrashReporterWin.cpp src/Update/InstallerWin.cpp)
elseif(APPLE)
    target_sources(cloudmus-core PRIVATE src/Diagnostics/CrashReporterPosix.cpp src/Update/InstallerMac.cpp)
else()
    target_sources(cloudmus-core PRIVATE src/Diagnostics/CrashReporterPosix.cpp src/Update/InstallerPosix.cpp)
endif()

# GCC 10 miscompiles our coroutines: a braced temporary inside a co_await
# expression (`co_await f(Params { a, b })`) is destroyed twice, which
# over-releases the QStrings in it and corrupts the heap much later, far
# from the cause. Builds fine and passes most tests, so refuse it outright.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR "GCC ${CMAKE_CXX_COMPILER_VERSION} miscompiles C++20 coroutines; "
        "use GCC 11+ or Clang (the AppImage build uses clang-19, see packaging/appimage/Dockerfile)")
endif()

# GCC needs this explicitly for C++20 coroutines (Rpc::Task<T>,
# co_await/co_return in RpcClient/PlaybackController/etc.) on older
# versions — a no-op on toolchains where coroutines are already enabled
# unconditionally under -std=c++20. PUBLIC: every user of Rpc::Task needs
# it too.
target_compile_options(cloudmus-core PUBLIC
    $<$<CXX_COMPILER_ID:GNU>:-fcoroutines>
)

target_compile_definitions(cloudmus-core PRIVATE
    CLOUDMUS_DEV_REPO_ROOT="${CLOUDMUS_REPO_ROOT}"
)

target_include_directories(cloudmus-core PUBLIC
    ${GENERATED_DIR}
    src/Rpc
    src/Playback
    src/Config
    src/I18n
    src/Net
    src/History
    src/Library
    src/Covers
    src/App
    src/Hotkeys
    src/ViewModel
    src/Diagnostics
    src/Update
    src/Timing
)

target_link_libraries(cloudmus-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    cloudmus-mpv
)

# Version.h (CLOUDMUS_VERSION) — regenerated on every build so a dev
# build's git commit stays current; see cmake/Version.cmake.
set(VERSION_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_version)

# Read the GA4 measurement ID from the environment; the generated header
# stays in the build directory and the value does not enter compiler
# command lines.
set(CLOUDMUS_GA4_MEASUREMENT_ID "$ENV{CLOUDMUS_GA4_MEASUREMENT_ID}")
if(CLOUDMUS_GA4_MEASUREMENT_ID AND NOT CLOUDMUS_GA4_MEASUREMENT_ID MATCHES "^G-[A-Z0-9]+$")
    message(FATAL_ERROR "Invalid GA4 Measurement ID")
endif()
configure_file(cmake/GA4Config.h.in ${VERSION_DIR}/GA4Config.h @ONLY)

# Sentry crash reports: only in a build that was given a DSN (the release
# builds; see cmake/SentryConfig.h.in). Without one nothing is fetched or
# linked, and the crash reporter stays local.
set(CLOUDMUS_SENTRY_DSN "$ENV{CLOUDMUS_SENTRY_DSN}")
if(CLOUDMUS_SENTRY_DSN AND NOT CLOUDMUS_SENTRY_DSN MATCHES "^https://[A-Za-z0-9]+(:[A-Za-z0-9]+)?@[A-Za-z0-9.-]+/[0-9]+$")
    message(FATAL_ERROR "Invalid Sentry DSN")
endif()
if(CLOUDMUS_SENTRY_DSN)
    set(CLOUDMUS_SENTRY_DEFAULT ON)
else()
    set(CLOUDMUS_SENTRY_DEFAULT OFF)
endif()
option(CLOUDMUS_SENTRY "Link sentry-native for crash reports" ${CLOUDMUS_SENTRY_DEFAULT})
configure_file(cmake/SentryConfig.h.in ${VERSION_DIR}/SentryConfig.h @ONLY)
if(CLOUDMUS_SENTRY)
    find_package(sentry CONFIG QUIET)
    if(NOT sentry_FOUND)
        include(FetchContent)
        # inproc: no crashpad_handler process to ship next to the exe, and
        # it builds with both clang on Debian 11 and MinGW.
        set(SENTRY_BACKEND inproc CACHE STRING "" FORCE)
        set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SENTRY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(sentry-native
            # The release archive has the submodules in it, and needs no git.
            URL https://github.com/getsentry/sentry-native/releases/download/0.9.1/sentry-native.zip
            DOWNLOAD_EXTRACT_TIMESTAMP ON)
        FetchContent_MakeAvailable(sentry-native)
    endif()
    target_link_libraries(cloudmus-core PUBLIC sentry::sentry)
    target_compile_definitions(cloudmus-core PUBLIC CLOUDMUS_HAS_SENTRY)
endif()

# TagLib writes the tags and cover of a saved track (Library::saveTrackFile).
# 2.2 or newer: WebM, which YouTube's audio often is, arrived in 2.2. Built
# statically from its release archive (utfcpp is in it) unless the system
# has a new enough one.
find_package(TagLib 2.2 CONFIG QUIET)
if(TagLib_FOUND)
    target_link_libraries(cloudmus-core PUBLIC TagLib::tag)
else()
    include(FetchContent)
    set(CLOUDMUS_BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(CLOUDMUS_BUILD_TESTING_SAVED ${BUILD_TESTING})
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
    set(WITH_ZLIB OFF CACHE BOOL "" FORCE) # only for compressed ID3v2 frames
    FetchContent_Declare(taglib
        URL https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz
        URL_HASH SHA256=3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(taglib)
    set(BUILD_SHARED_LIBS ${CLOUDMUS_BUILD_SHARED_LIBS_SAVED})
    set(BUILD_TESTING ${CLOUDMUS_BUILD_TESTING_SAVED})
    # In the tree TagLib's headers sit in a directory per format and find
    # each other by bare name (they are flattened only when installed).
    file(GLOB_RECURSE _taglib_headers "${taglib_SOURCE_DIR}/taglib/*.h")
    set(_taglib_dirs "${taglib_BINARY_DIR}")
    foreach(_header ${_taglib_headers})
        get_filename_component(_dir "${_header}" DIRECTORY)
        list(APPEND _taglib_dirs "${_dir}")
    endforeach()
    list(REMOVE_DUPLICATES _taglib_dirs)
    # PUBLIC: the track file tests read the tags back with it.
    target_include_directories(cloudmus-core PUBLIC ${_taglib_dirs})
    target_link_libraries(cloudmus-core PUBLIC tag)
endif()
add_custom_target(cloudmus_version
    COMMAND ${CMAKE_COMMAND} -DBASE=${PROJECT_VERSION} -DSRC=${CLOUDMUS_REPO_ROOT} -DOUT=${VERSION_DIR}/Version.h
            -DVERSION_OVERRIDE=${CLOUDMUS_VERSION_OVERRIDE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake
    BYPRODUCTS ${VERSION_DIR}/Version.h
    COMMENT "Updating Version.h"
)

# --- cloudmus-ui: the widgets and the desktop integrations on top of the
# core — everything of the app but main() and its resources, so the window
# tests can link it too.
set(CLOUDMUS_PLATFORM_SOURCES)
if(WIN32)
    list(APPEND CLOUDMUS_PLATFORM_SOURCES
        src/Integration/AppIdentityWin.cpp
        src/Integration/AutostartWin.cpp
        src/Integration/FirstFrameBackgroundWin.cpp
        src/Integration/NotificationToastWin.cpp
        src/Integration/SmtcService.cpp
        src/Integration/TaskbarThumbButtons.cpp
        src/Integration/GlobalHotkeysWin.cpp)
elseif(APPLE)
    # The macOS integrations, some still stubs (*Mac.cpp). The headers are
    # listed for AUTOMOC: it pairs a header with the .cpp of the same name,
    # and these have none.
    list(APPEND CLOUDMUS_PLATFORM_SOURCES
        src/Integration/AutostartMac.mm
        src/Integration/NotificationToast.h
        src/Integration/NotificationToastMac.mm
        src/Integration/DesktopColorScheme.h
        src/Integration/DesktopColorSchemeMac.cpp
        src/Integration/DockIcon.h
        src/Integration/DockIconMac.mm
        src/Integration/DockTile.h
        src/Integration/DockTileMac.cpp
        src/Integration/NowPlayingCenter.h
        src/Integration/NowPlayingCenterMac.mm
        src/Integration/WindowGlassMac.mm
        src/Integration/GlobalHotkeysMac.cpp
        src/Integration/QuitSignals.cpp)
else()
    list(APPEND CLOUDMUS_PLATFORM_SOURCES
        src/Integration/Autostart.cpp
        src/Integration/NotificationToast.cpp
        src/Integration/MprisService.cpp
        src/Integration/DesktopColorScheme.cpp
        src/Integration/QuitSignals.cpp
        src/Integration/GlobalHotkeysKGlobalAccel.cpp
        src/Integration/GlobalHotkeysPortal.cpp)
endif()

add_library(cloudmus-ui STATIC
    src/Ui/MainWindow.cpp
    src/Ui/WindowHost.cpp
    src/Ui/DownloadsPanel.cpp
    src/Ui/AboutDialog.cpp
    src/Ui/CloudsBackdrop.cpp
    src/Ui/PulsingLogo.cpp
    src/Ui/InfoDialog.cpp
    src/Ui/UpdateDialog.cpp
    src/Ui/UpdateFlow.cpp
    src/Ui/StarPromptFlow.cpp
    src/Ui/NavItemDelegate.cpp
    src/Ui/SidebarTreeView.cpp
    src/Ui/TrackListModel.cpp
    src/Ui/TrackRowDelegate.cpp
    src/Ui/NowPlayingBar.cpp
    src/Ui/HeroPanel.cpp
    src/Ui/EmptyStatePlaceholder.cpp
    src/Ui/GeneratedCoverArt.cpp
    src/Ui/SourcePanel.cpp
    src/Ui/AuthCard.cpp
    src/Ui/MiniHtml.cpp
    src/Ui/ToastNotifier.cpp
    src/Ui/SmoothScroller.cpp
    src/Ui/OverlayScrollBar.cpp
    src/Ui/ThemedSlider.cpp
    src/Ui/PreloadCells.cpp
    src/Ui/TabOrder.cpp
    src/Ui/KeyActivation.cpp
    src/Ui/ThemeCrossfade.cpp
    src/Ui/Transition.cpp
    src/Ui/TrackHoverCard.cpp
    src/Ui/TextLayout.cpp
    src/Ui/ScrollEdgeFade.cpp
    src/Ui/FrameClock.cpp
    src/Ui/MenuCheckRow.cpp
    src/Theme/Shadow.cpp
    src/Ui/AnimatedPresenter.cpp
    src/Ui/PlaylistSheet.cpp
    src/Ui/ThemedToolTip.cpp
    src/Ui/SettingsDialog.cpp
    src/Ui/DialogButtons.cpp
    src/Ui/PasswordReveal.cpp
    src/Ui/Settings/Page.h
    src/Ui/Settings/PageStack.cpp
    src/Ui/Settings/GeneralPage.cpp
    src/Ui/Settings/DownloadsPage.cpp
    src/Ui/Settings/PreloadPage.cpp
    src/Ui/Settings/SourcePage.cpp
    src/Ui/Settings/SettingsForm.cpp
    src/Ui/Settings/NetworkPage.cpp
    src/Ui/Settings/HotkeysPage.cpp
    src/Ui/AppShortcuts.cpp

    src/Theme/Tokens.cpp
    src/Theme/Typography.cpp
    src/Theme/Fonts.cpp
    src/Theme/Icons.cpp
    src/Theme/CoverPlaceholder.cpp
    src/Theme/StyleSheet.cpp
    src/Theme/Style.cpp
    src/Theme/FocusRing.cpp
    src/Theme/PopupWindow.cpp

    src/Integration/TrayIcon.cpp
    src/Integration/GlobalHotkeys.cpp
    src/Integration/KeyNames.cpp
    ${CLOUDMUS_PLATFORM_SOURCES}
    src/Integration/WindowGlass.cpp

)
add_dependencies(cloudmus-ui cloudmus_version)

target_include_directories(cloudmus-ui PUBLIC
    ${VERSION_DIR}
    src/Ui
    src/Theme
    src/Integration
)

target_link_libraries(cloudmus-ui PUBLIC
    cloudmus-core
    Qt6::Widgets
    Qt6::Svg
)

# Precompiled headers: most of a translation unit's compile time is the Qt
# and standard headers every file pulls in, so they are parsed once per
# library. Only system headers go in — nothing that changes with a commit —
# so a PCH is the same bytes on every machine and ccache can reuse objects.
# The core list has no Widgets (see fronts/qt/AGENTS.md).
option(CLOUDMUS_PCH "Precompile the Qt and standard headers" ON)
if(CLOUDMUS_PCH)
    set(CLOUDMUS_PCH_CORE
        QByteArray QCoreApplication QDateTime QDir QFile QFileInfo
        QHash QJsonArray QJsonDocument QJsonObject QList QLoggingCategory
        QMap QObject QPointer QSet QString QStringList QTimer QUrl
        QVariant QVector
        algorithm functional memory optional utility vector)
    set(CLOUDMUS_PCH_UI
        QApplication QColor QDialog QFont QGuiApplication QHBoxLayout
        QIcon QLabel QMouseEvent QPainter QPainterPath QPixmap
        QPushButton QRect QVBoxLayout QWidget)
    # C++ only: cloudmus-ui has C sources too (the Wayland protocol), and
    # without the condition CMake builds a C PCH from the same C++ headers.
    # One expression per header, with $<ANGLE-R> for the closing bracket: a
    # literal ">" would end the generator expression.
    set(_pch_core)
    foreach(h ${CLOUDMUS_PCH_CORE})
        list(APPEND _pch_core "$<$<COMPILE_LANGUAGE:CXX>:<${h}$<ANGLE-R>>")
    endforeach()
    set(_pch_ui)
    foreach(h ${CLOUDMUS_PCH_UI})
        list(APPEND _pch_ui "$<$<COMPILE_LANGUAGE:CXX>:<${h}$<ANGLE-R>>")
    endforeach()
    target_precompile_headers(cloudmus-core PRIVATE ${_pch_core})
    target_precompile_headers(cloudmus-ui PRIVATE ${_pch_core} ${_pch_ui})
    if(CLOUDMUS_CCACHE_PROGRAM AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Keeps the PCH's bytes independent of when it was written.
        target_compile_options(cloudmus-core PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
        target_compile_options(cloudmus-ui PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
    endif()
endif()
if(TARGET Qt6::DBus)
    target_link_libraries(cloudmus-ui PUBLIC Qt6::DBus)
endif()
if(WIN32)
    # Tray balloons sent directly (NotificationToastWin.cpp); DWM for
    # first-frame background (FirstFrameBackgroundWin.cpp) and glass (WindowGlass.cpp);
    # runtimeobject for WinRT activation (Ro*), shcore + shlwapi to hand a
    # cover to SMTC without an http(s) URL (SmtcService.cpp); ole32 for
    # CoCreateInstance(ITaskbarList3) (TaskbarThumbButtons.cpp).
    target_link_libraries(cloudmus-ui PRIVATE shell32 dwmapi runtimeobject shcore shlwapi ole32)
endif()
if(APPLE)
    # The Dock's icon and the app's presence there (DockIconMac.mm); Now
    # Playing and the media keys (NowPlayingCenterMac.mm); notifications
    # (NotificationToastMac.mm); glass (WindowGlassMac.mm); global hotkeys
    # (Carbon, GlobalHotkeysMac.cpp); the login agent (AutostartMac.mm,
    # Foundation, which AppKit brings). The precompiled Qt headers are C++'s; the
    # Objective-C++ files do without. The newer ones are built with ARC.
    target_link_libraries(cloudmus-ui PRIVATE
        "-framework AppKit" "-framework Carbon" "-framework MediaPlayer" "-framework UserNotifications")
    set_source_files_properties(src/Integration/DockIconMac.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    set_source_files_properties(src/Integration/NowPlayingCenterMac.mm src/Integration/NotificationToastMac.mm
        src/Integration/WindowGlassMac.mm src/Integration/AutostartMac.mm
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS -fobjc-arc)
endif()

# Glass (Integration::WindowGlass): blur asked of the compositor directly —
# over Wayland (the standard ext-background-effect-v1 or KWin's
# org_kde_kwin_blur; needs Qt's private headers for a window's wl_surface,
# plus wayland-client and wayland-scanner) and KWin's X11 window property
# (xcb). Each is optional; without them, the system's KF6WindowSystem is
# loaded at runtime instead, which works only next to the Qt it was built
# against (not in the AppImage).
find_package(Qt6 QUIET OPTIONAL_COMPONENTS GuiPrivate)
if(NOT WIN32)
    pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
    find_program(WAYLAND_SCANNER wayland-scanner)
endif()
if(TARGET Qt6::GuiPrivate AND WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER)
    set(BLUR_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_wayland)
    # Protocol copies in third_party/: the distro's wayland-protocols is
    # too old for ext-background-effect on the AppImage's Debian 11 base.
    foreach(protocol ext-background-effect-v1 org-kde-kwin-blur)
        set(xml ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wayland/${protocol}.xml)
        add_custom_command(
            OUTPUT ${BLUR_GENERATED_DIR}/${protocol}-client-protocol.h
                   ${BLUR_GENERATED_DIR}/${protocol}-protocol.c
            COMMAND ${CMAKE_COMMAND} -E make_directory ${BLUR_GENERATED_DIR}
            COMMAND ${WAYLAND_SCANNER} client-header ${xml}
                    ${BLUR_GENERATED_DIR}/${protocol}-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} private-code ${xml}
                    ${BLUR_GENERATED_DIR}/${protocol}-protocol.c
            DEPENDS ${xml}
            VERBATIM
        )
        target_sources(cloudmus-ui PRIVATE
            ${BLUR_GENERATED_DIR}/${protocol}-client-protocol.h
            ${BLUR_GENERATED_DIR}/${protocol}-protocol.c)
    endforeach()
    target_include_directories(cloudmus-ui PRIVATE ${BLUR_GENERATED_DIR})
    target_compile_definitions(cloudmus-ui PRIVATE CLOUDMUS_BLUR_WAYLAND)
    target_link_libraries(cloudmus-ui PRIVATE Qt6::GuiPrivate PkgConfig::WAYLAND_CLIENT)
    message(STATUS "Glass: Wayland blur protocols, built in")
endif()
if(NOT WIN32)
    pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
endif()
if(XCB_FOUND)
    target_compile_definitions(cloudmus-ui PRIVATE CLOUDMUS_BLUR_X11 CLOUDMUS_HOTKEYS_X11)
    target_sources(cloudmus-ui PRIVATE src/Integration/GlobalHotkeysX11.cpp)
    target_link_libraries(cloudmus-ui PRIVATE PkgConfig::XCB)
    message(STATUS "Glass: KWin's X11 blur property, built in; global hotkeys: X11 key grabs, built in")
endif()

add_executable(cloudmus-qt
    src/main.cpp
    src/Logging.cpp

    resources/icons.qrc
    resources/fonts.qrc
    resources/about.qrc
    resources/sounds.qrc
)
if(WIN32)
    set_target_properties(cloudmus-qt PROPERTIES WIN32_EXECUTABLE TRUE)
    set(CLOUDMUS_ICON_PATH "${CLOUDMUS_REPO_ROOT}/packaging/windows/cloudmus.ico")
    configure_file(resources/cloudmus.rc.in "${CMAKE_CURRENT_BINARY_DIR}/cloudmus.rc" @ONLY)
    target_sources(cloudmus-qt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cloudmus.rc")
    # Takes the version from Version.h, which is regenerated on every build.
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/cloudmus.rc" PROPERTIES
        OBJECT_DEPENDS "${VERSION_DIR}/Version.h")
    add_executable(cloudmus-yt-dlp WIN32 src/Playback/YtDlpLauncherWin.cpp)
    set_target_properties(cloudmus-yt-dlp PROPERTIES OUTPUT_NAME "yt-dlp")
elseif(APPLE)
    # CloudMus.app: macOS names an app after its bundle, a bare executable
    # after its file ("cloudmus-qt" in the Dock and the menu bar). Now
    # Playing and notifications identify the app by its bundle too.
    set_target_properties(cloudmus-qt PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME CloudMus
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME CloudMus
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.cloudmus.CloudMus
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE AppIcon)
    # The bundle's icon (Finder, notifications), drawn from the logo by a
    # tool built for this machine and packed by iconutil.
    add_executable(cloudmus-make-iconset tools/MakeIconset.cpp)
    target_link_libraries(cloudmus-make-iconset PRIVATE Qt6::Gui Qt6::Svg)
    set(_iconset "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.iconset")
    set(_icns "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")
    add_custom_command(OUTPUT ${_icns}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_iconset}
        COMMAND cloudmus-make-iconset ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/logo.svg ${_iconset}
        COMMAND iconutil -c icns -o ${_icns} ${_iconset}
        DEPENDS cloudmus-make-iconset ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/logo.svg
        VERBATIM)
    target_sources(cloudmus-qt PRIVATE ${_icns})
    set_source_files_properties(${_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    # The linker signs the executable alone; notifications want the whole
    # bundle signed, ad hoc is enough on the machine that built it. Signed
    # on every build (a fraction of a second), not after linking only: a
    # changed Info.plist or icon breaks the signature as well.
    add_custom_target(cloudmus-qt-sign ALL
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:cloudmus-qt>
        DEPENDS cloudmus-qt
        VERBATIM)
endif()
# The app's own texts, one translations/cloudmus_<code>.ts per language
# (English is the source language), compiled to .qm and embedded under
# :/i18n — where I18n::Translator looks. After changing a text:
# `cmake --build build --target update_translations`.
# lupdate also reads the sources this platform doesn't build: with
# -no-obsolete it would drop their texts (InstallerWin.cpp on Linux,
# InstallerPosix.cpp on Windows, InstallerMac.cpp on both) along with the
# translations. Only those —
# a file given twice confuses lupdate's namespaces.
file(GLOB_RECURSE _tr_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
foreach(_target cloudmus-core cloudmus-ui cloudmus-qt)
    get_target_property(_target_sources ${_target} SOURCES)
    foreach(_source IN LISTS _target_sources)
        cmake_path(ABSOLUTE_PATH _source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        list(REMOVE_ITEM _tr_sources "${_source}")
    endforeach()
endforeach()
# Qt would list the languages in CFBundleLocalizations from the .ts files,
# but reads them with an xmllint that prints to stderr on newer macOS and
# takes that for a failure; resources/Info.plist.in lists them instead.
set(QT_SKIP_STORE_LANGUAGES_FROM_TS_FILES ON)
qt_add_translations(cloudmus-qt
    TS_FILES
        translations/cloudmus_be.ts
        translations/cloudmus_de.ts
        translations/cloudmus_es.ts
        translations/cloudmus_fr.ts
        translations/cloudmus_it.ts
        translations/cloudmus_ru.ts
    SOURCE_TARGETS cloudmus-core cloudmus-ui cloudmus-qt
    SOURCES ${_tr_sources}
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -source-language en
)
# Qt's own catalogs (the Ok/Cancel/Yes/No of its stock dialogs) are embedded
# next to ours: a bundled Qt (AppImage) may ship none, and
# I18n::Translator falls back to :/i18n. Belarusian has no Qt catalog.
# The path is relative to the prefix, except where a distro (nixpkgs) ships
# the catalogs as a separate package and gives it absolute.
cmake_path(ABSOLUTE_PATH QT6_INSTALL_TRANSLATIONS
    BASE_DIRECTORY "${QT6_INSTALL_PREFIX}" OUTPUT_VARIABLE _qt_tr_dir)
set(_qt_tr_files)
foreach(_lang de es fr it ru)
    if(EXISTS "${_qt_tr_dir}/qtbase_${_lang}.qm")
        list(APPEND _qt_tr_files "${_qt_tr_dir}/qtbase_${_lang}.qm")
    else()
        message(WARNING "No qtbase_${_lang}.qm in ${_qt_tr_dir}: Qt's own dialogs stay English for ${_lang}")
    endif()
endforeach()
if(_qt_tr_files)
    set(_qt_tr_qrc "${CMAKE_CURRENT_BINARY_DIR}/qt_translations.qrc")
    set(_qrc_body "<RCC><qresource prefix=\"/i18n\">\n")
    foreach(_f ${_qt_tr_files})
        get_filename_component(_n "${_f}" NAME)
        string(APPEND _qrc_body "<file alias=\"${_n}\">${_f}</file>\n")
    endforeach()
    string(APPEND _qrc_body "</qresource></RCC>\n")
    file(GENERATE OUTPUT "${_qt_tr_qrc}" CONTENT "${_qrc_body}")
    set_source_files_properties("${_qt_tr_qrc}" PROPERTIES GENERATED TRUE)
    target_sources(cloudmus-qt PRIVATE "${_qt_tr_qrc}")
endif()
target_include_directories(cloudmus-qt PRIVATE ${VERSION_DIR})
add_dependencies(cloudmus-qt cloudmus_version)
target_link_libraries(cloudmus-qt PRIVATE cloudmus-ui)
if(WIN32)
    # SetCurrentProcessExplicitAppUserModelID(), called from main.cpp itself.
    target_link_libraries(cloudmus-qt PRIVATE shell32)
endif()
# Crash reports' stacks name the app's own functions only if they're in
# the dynamic symbol table (backtrace_symbols_fd reads that, not debug info).
if(NOT WIN32)
    set_target_properties(cloudmus-qt PROPERTIES ENABLE_EXPORTS ON)
endif()
# Sentry finds the symbols for a crash by the executable's build id, so the
# one in the release must match the unstripped copy uploaded from CI.
# Apple's linker has no --build-id (Sentry isn't built for macOS yet).
if(CLOUDMUS_SENTRY AND NOT APPLE)
    target_link_options(cloudmus-qt PRIVATE -Wl,--build-id)
endif()

# --- cloudmus-download-bench: how fast Net::ParallelDownload gets a file
# over N connections. A developer's tool, built only when asked for:
# `cmake --build build --target cloudmus-download-bench`.
add_executable(cloudmus-download-bench EXCLUDE_FROM_ALL tools/DownloadBench.cpp)
target_link_libraries(cloudmus-download-bench PRIVATE cloudmus-core)

# --- tests (QtTest): `ctest --test-dir build`, or run build/bin/cloudmus-qt-tests.
if(BUILD_TESTING)
add_executable(cloudmus-qt-tests
    tests/main.cpp
    tests/FakeBackend.cpp
    tests/RpcClientTest.cpp
    tests/SourceSessionTest.cpp
    tests/NowPlayingTest.cpp
    tests/PlaylistEditingTest.cpp
    tests/SourcesTest.cpp
    tests/ActivePlaylistTest.cpp
    tests/BrowseTest.cpp
    tests/DownloadsTest.cpp
    tests/AnalyticsTest.cpp
    tests/StreamRelayTest.cpp
    tests/TrackCacheTest.cpp
    tests/AudioFileProbeTest.cpp
    tests/PreloaderTest.cpp
    tests/PlaybackPreloadTest.cpp
    tests/PreloadTest.cpp
    tests/TrackFileTest.cpp
    tests/CrashReporterTest.cpp
    tests/UpdateTest.cpp
    tests/ParallelDownloadTest.cpp
    tests/HotkeysTest.cpp
    tests/I18nTest.cpp
    tests/AudioPulseTest.cpp
    tests/StarPromptTest.cpp
    tests/PathsTest.cpp
    tests/TrackStatesTest.cpp
    tests/TimingTest.cpp
    tests/SequenceTest.cpp
)
target_include_directories(cloudmus-qt-tests PRIVATE tests)
target_link_libraries(cloudmus-qt-tests PRIVATE cloudmus-core Qt6::Test)
# CrashReporterTest checks that a crash report names the crashing function.
set_target_properties(cloudmus-qt-tests PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME cloudmus-qt-tests COMMAND cloudmus-qt-tests)

# The window's own tests — widgets, run without a screen (offscreen).
add_executable(cloudmus-qt-ui-tests
    tests/ui/main.cpp
    tests/ui/WindowHostTest.cpp
    tests/ui/FocusTest.cpp
    tests/ui/HotkeysPageTest.cpp
    tests/ui/SourcePageTest.cpp
    tests/ui/PreloadUiTest.cpp
    tests/ui/AboutDialogTest.cpp
    # Covers are QPixmaps, which need a GUI application.
    tests/ui/CoverArtCacheTest.cpp
    tests/ui/DisabledLookTest.cpp
    tests/ui/AutostartTest.cpp
    tests/FakeBackend.cpp
    # Real glyphs, so a test can check what color an icon is tinted.
    resources/icons.qrc
)
target_include_directories(cloudmus-qt-ui-tests PRIVATE tests)
target_link_libraries(cloudmus-qt-ui-tests PRIVATE cloudmus-ui Qt6::Test)
add_test(NAME cloudmus-qt-ui-tests COMMAND cloudmus-qt-ui-tests)
set_tests_properties(cloudmus-qt-ui-tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
