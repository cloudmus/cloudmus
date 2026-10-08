cmake_minimum_required(VERSION 3.21)
project(cloudmus-qt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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
    src/Playback/StreamRelay.cpp
    src/Playback/PlaybackController.cpp
    src/Playback/BeatDetector.cpp
    src/Playback/LevelFeed.cpp
    src/Playback/CuePlayer.cpp

    src/Config/Settings.cpp
    src/I18n/Languages.cpp
    src/I18n/Translator.cpp
    src/Net/ProxyRouting.cpp

    src/History/PlaybackHistory.cpp
    src/Library/TrackStates.cpp
    src/Library/DownloadPaths.cpp

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

    src/Diagnostics/CrashReporter.cpp
    src/Diagnostics/CrashShortcut.cpp

    src/Update/ReleaseFeed.cpp
    src/Update/UpdateChecker.cpp
    src/Update/UpdateDownloader.cpp

    ${GENERATED_DIR}/RpcMethods.cpp
)
add_dependencies(cloudmus-core protocol_codegen_cpp)
if(WIN32)
    target_sources(cloudmus-core PRIVATE src/Diagnostics/CrashReporterWin.cpp src/Update/InstallerWin.cpp)
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
        src/Integration/AutostartWin.cpp
        src/Integration/FirstFrameBackgroundWin.cpp
        src/Integration/NotificationToastWin.cpp
        src/Integration/SmtcService.cpp
        src/Integration/TaskbarThumbButtons.cpp
        src/Integration/GlobalHotkeysWin.cpp)
else()
    list(APPEND CLOUDMUS_PLATFORM_SOURCES
        src/Integration/Autostart.cpp
        src/Integration/NotificationToast.cpp
        src/Integration/MprisService.cpp
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
endif()
# The app's own texts, one translations/cloudmus_<code>.ts per language
# (English is the source language), compiled to .qm and embedded under
# :/i18n — where I18n::Translator looks. After changing a text:
# `cmake --build build --target update_translations`.
qt_add_translations(cloudmus-qt
    TS_FILES
        translations/cloudmus_be.ts
        translations/cloudmus_de.ts
        translations/cloudmus_es.ts
        translations/cloudmus_fr.ts
        translations/cloudmus_it.ts
        translations/cloudmus_ru.ts
    SOURCE_TARGETS cloudmus-core cloudmus-ui cloudmus-qt
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -source-language en
)
# Qt's own catalogs (the Ok/Cancel/Yes/No of its stock dialogs) are embedded
# next to ours: a bundled Qt (AppImage) may ship none, and
# I18n::Translator falls back to :/i18n. Belarusian has no Qt catalog.
set(_qt_tr_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
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
if(CLOUDMUS_SENTRY)
    target_link_options(cloudmus-qt PRIVATE -Wl,--build-id)
endif()

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
    tests/CrashReporterTest.cpp
    tests/UpdateTest.cpp
    tests/HotkeysTest.cpp
    tests/I18nTest.cpp
    tests/AudioPulseTest.cpp
    tests/StarPromptTest.cpp
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
    tests/ui/AboutDialogTest.cpp
    tests/FakeBackend.cpp
    # Real glyphs, so a test can check what color an icon is tinted.
    resources/icons.qrc
)
target_include_directories(cloudmus-qt-ui-tests PRIVATE tests)
target_link_libraries(cloudmus-qt-ui-tests PRIVATE cloudmus-ui Qt6::Test)
add_test(NAME cloudmus-qt-ui-tests COMMAND cloudmus-qt-ui-tests)
set_tests_properties(cloudmus-qt-ui-tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
