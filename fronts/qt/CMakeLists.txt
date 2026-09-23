cmake_minimum_required(VERSION 3.21)
project(cloudmus-qt CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network DBus Svg)

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

add_custom_command(
    OUTPUT ${GENERATED_CPP_STAMP}
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

add_executable(cloudmus-qt
    src/main.cpp
    src/Logging.cpp

    src/Rpc/NdjsonTransport.cpp
    src/Rpc/BackendManifest.cpp
    src/Rpc/RpcClient.cpp
    src/Rpc/SourceManager.cpp

    src/Playback/AudioPlayer.cpp
    src/Playback/PlaybackController.cpp

    src/Config/Settings.cpp

    src/History/PlaybackHistory.cpp

    src/Ui/MainWindow.cpp
    src/Ui/AboutDialog.cpp
    src/Ui/SidebarModel.cpp
    src/Ui/NavItemDelegate.cpp
    src/Ui/TrackListModel.cpp
    src/Ui/TrackRowDelegate.cpp
    src/Ui/NowPlayingBar.cpp
    src/Ui/HeroPanel.cpp
    src/Ui/EmptyStatePlaceholder.cpp
    src/Ui/GeneratedCoverArt.cpp
    src/Ui/SourcePanel.cpp
    src/Ui/ToastNotifier.cpp
    src/Ui/SmoothScroller.cpp
    src/Ui/OverlayScrollBar.cpp
    src/Ui/ThemedSplitter.cpp
    src/Ui/ThemedSlider.cpp
    src/Ui/Transition.cpp
    src/Ui/ThemedToolTip.cpp
    src/Ui/SettingsDialog.cpp
    src/Ui/CoverArtCache.cpp

    src/Theme/Tokens.cpp
    src/Theme/Typography.cpp
    src/Theme/Fonts.cpp
    src/Theme/Icons.cpp
    src/Theme/CoverPlaceholder.cpp
    src/Theme/StyleSheet.cpp
    src/Theme/Style.cpp

    src/Integration/TrayIcon.cpp
    src/Integration/NotificationToast.cpp
    src/Integration/MprisService.cpp
    src/Integration/GlobalShortcuts.cpp

    ${GENERATED_DIR}/RpcMethods.cpp

    resources/icons.qrc
    resources/fonts.qrc
)
add_dependencies(cloudmus-qt protocol_codegen_cpp)

# GCC needs this explicitly for C++20 coroutines (Rpc::Task<T>,
# co_await/co_return in RpcClient/PlaybackController/etc.) on the GCC
# versions older, still-widely-deployed distros ship (e.g. GCC 10 on
# Debian 11, used for the AppImage build — see packaging/appimage/) — a
# no-op on toolchains where coroutines are already enabled unconditionally
# under -std=c++20.
target_compile_options(cloudmus-qt PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-fcoroutines>
)

target_compile_definitions(cloudmus-qt PRIVATE
    CLOUDMUS_DEV_REPO_ROOT="${CLOUDMUS_REPO_ROOT}"
)

target_include_directories(cloudmus-qt PRIVATE
    ${GENERATED_DIR}
    src/Rpc
    src/Playback
    src/Ui
    src/Theme
    src/Integration
    src/Config
    src/History
)

target_link_libraries(cloudmus-qt PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    Qt6::Svg
    PkgConfig::MPV
)
