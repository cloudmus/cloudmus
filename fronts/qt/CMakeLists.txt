cmake_minimum_required(VERSION 3.21)
project(cloudmus-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network DBus Svg Test)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

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

if(NOT PYTHON3_EXECUTABLE)
    if(EXISTS ${CLOUDMUS_REPO_ROOT}/.venv/bin/python3)
        set(PYTHON3_EXECUTABLE ${CLOUDMUS_REPO_ROOT}/.venv/bin/python3)
    else()
        find_program(PYTHON3_EXECUTABLE NAMES python3 python REQUIRED)
    endif()
endif()
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set(GENERATED_CPP_STAMP ${CMAKE_CURRENT_BINARY_DIR}/generated_cpp.stamp)

file(GLOB_RECURSE PROTOCOL_SCHEMA_FILES ${CLOUDMUS_REPO_ROOT}/protocol/schema/*.yaml)

# The generated sources are listed as outputs, not just the stamp: on a
# fresh checkout they don't exist yet (gitignored), and CMake refuses to
# configure with a missing source file unless it knows a rule produces it.
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

# --- cloudmus-core: everything below the view — backends, playback, state,
# settings — with no Qt Widgets, so the linker keeps widget code out of it
# (see docs/mvvm-plan.md). Linked by the app and by the tests.
add_library(cloudmus-core STATIC
    src/Rpc/NdjsonTransport.cpp
    src/Rpc/BackendManifest.cpp
    src/Rpc/RpcClient.cpp
    src/Rpc/SourceManager.cpp
    src/Rpc/AuthStates.cpp

    src/Playback/AudioPlayer.cpp
    src/Playback/StreamRelay.cpp
    src/Playback/PlaybackController.cpp

    src/Config/Settings.cpp
    src/Net/ProxyRouting.cpp

    src/History/PlaybackHistory.cpp
    src/Library/TrackStates.cpp
    src/Library/DownloadPaths.cpp

    src/Covers/CoverArtCache.cpp

    src/App/Core.cpp
    src/App/SourceSession.cpp
    src/App/PlaylistEditing.cpp
    src/App/TrackFetch.cpp

    src/ViewModel/Messages.cpp
    src/ViewModel/NowPlaying.cpp
    src/ViewModel/SidebarModel.cpp
    src/ViewModel/Sources.cpp
    src/ViewModel/ActivePlaylist.cpp

    ${GENERATED_DIR}/RpcMethods.cpp
)
add_dependencies(cloudmus-core protocol_codegen_cpp)

# GCC needs this explicitly for C++20 coroutines (Rpc::Task<T>,
# co_await/co_return in RpcClient/PlaybackController/etc.) on the GCC
# versions older, still-widely-deployed distros ship (e.g. GCC 10 on
# Debian 11, used for the AppImage build — see packaging/appimage/) — a
# no-op on toolchains where coroutines are already enabled unconditionally
# under -std=c++20. PUBLIC: every user of Rpc::Task needs it too.
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
    src/Net
    src/History
    src/Library
    src/Covers
    src/App
    src/ViewModel
)

target_link_libraries(cloudmus-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    PkgConfig::MPV
)

add_executable(cloudmus-qt
    src/main.cpp
    src/Logging.cpp

    src/Ui/MainWindow.cpp
    src/Ui/AboutDialog.cpp
    src/Ui/InfoDialog.cpp
    src/Ui/NavItemDelegate.cpp
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
    src/Ui/Transition.cpp
    src/Ui/TrackHoverCard.cpp
    src/Ui/TextLayout.cpp
    src/Ui/ScrollEdgeFade.cpp
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

    src/Theme/Tokens.cpp
    src/Theme/Typography.cpp
    src/Theme/Fonts.cpp
    src/Theme/Icons.cpp
    src/Theme/CoverPlaceholder.cpp
    src/Theme/StyleSheet.cpp
    src/Theme/Style.cpp

    src/Integration/TrayIcon.cpp
    src/Integration/Autostart.cpp
    src/Integration/NotificationToast.cpp
    src/Integration/MprisService.cpp
    src/Integration/GlobalShortcuts.cpp
    src/Integration/WindowGlass.cpp

    resources/icons.qrc
    resources/fonts.qrc
    resources/about.qrc
)
add_dependencies(cloudmus-qt protocol_codegen_cpp)

# Version.h (CLOUDMUS_VERSION) — regenerated on every build so a dev
# build's git commit stays current; see cmake/Version.cmake.
set(VERSION_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_version)
add_custom_target(cloudmus_version
    COMMAND ${CMAKE_COMMAND} -DBASE=${PROJECT_VERSION} -DSRC=${CLOUDMUS_REPO_ROOT} -DOUT=${VERSION_DIR}/Version.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake
    BYPRODUCTS ${VERSION_DIR}/Version.h
    COMMENT "Updating Version.h"
)
add_dependencies(cloudmus-qt cloudmus_version)

target_include_directories(cloudmus-qt PRIVATE
    ${VERSION_DIR}
    src/Ui
    src/Theme
    src/Integration
)

target_link_libraries(cloudmus-qt PRIVATE
    cloudmus-core
    Qt6::Widgets
    Qt6::DBus
    Qt6::Svg
)

# --- tests (QtTest): `ctest --test-dir build`, or run build/bin/cloudmus-qt-tests.
enable_testing()
add_executable(cloudmus-qt-tests
    tests/main.cpp
    tests/FakeBackend.cpp
    tests/RpcClientTest.cpp
    tests/SourceSessionTest.cpp
    tests/NowPlayingTest.cpp
    tests/PlaylistEditingTest.cpp
    tests/SourcesTest.cpp
    tests/ActivePlaylistTest.cpp
)
target_include_directories(cloudmus-qt-tests PRIVATE tests)
target_link_libraries(cloudmus-qt-tests PRIVATE cloudmus-core Qt6::Test)
add_test(NAME cloudmus-qt-tests COMMAND cloudmus-qt-tests)
