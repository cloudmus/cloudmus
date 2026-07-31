cmake_minimum_required(VERSION 3.21)
project(cloudmus-qt CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network DBus Multimedia Svg)

# --- protocol/codegen: regenerate fronts/qt/generated/ whenever the schema
# or methods change, before anything tries to #include it. Requires Python +
# jinja2/pyyaml at configure/build time — see protocol/README.md and the
# plan's "Generated code: gitignored" note for why this tradeoff was made.
# Prefer the repo's own .venv (where the rest of the project already
# installs jinja2/pyyaml, see backends/py-rpc-common) over a bare system
# python3 that likely doesn't have them.
set(CLOUDMUS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(CLOUDMUS_REPO_ROOT ${CLOUDMUS_REPO_ROOT} ABSOLUTE)

if(EXISTS ${CLOUDMUS_REPO_ROOT}/.venv/bin/python3)
    set(PYTHON3_EXECUTABLE ${CLOUDMUS_REPO_ROOT}/.venv/bin/python3)
else()
    find_program(PYTHON3_EXECUTABLE NAMES python3 python REQUIRED)
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
    src/Ui/ClickableArea.h  # header-only Q_OBJECT class, no matching .cpp — must be listed explicitly for AUTOMOC
    src/Ui/SidebarModel.cpp
    src/Ui/TrackListModel.cpp
    src/Ui/TrackRowDelegate.cpp
    src/Ui/NowPlayingBar.cpp
    src/Ui/PlaylistHeader.cpp
    src/Ui/AuthBanner.cpp
    src/Ui/ToastNotifier.cpp
    src/Ui/SettingsDialog.cpp
    src/Ui/CoverArtCache.cpp

    src/Integration/TrayIcon.cpp
    src/Integration/NotificationToast.cpp
    src/Integration/MprisService.cpp
    src/Integration/GlobalShortcuts.cpp

    ${GENERATED_DIR}/RpcMethods.cpp

    resources/icons.qrc
)
add_dependencies(cloudmus-qt protocol_codegen_cpp)

target_compile_definitions(cloudmus-qt PRIVATE
    CLOUDMUS_DEV_REPO_ROOT="${CLOUDMUS_REPO_ROOT}"
)

target_include_directories(cloudmus-qt PRIVATE
    ${GENERATED_DIR}
    src/Rpc
    src/Playback
    src/Ui
    src/Integration
    src/Config
    src/History
)

target_link_libraries(cloudmus-qt PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    Qt6::Multimedia
    Qt6::Svg
)
