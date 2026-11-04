# cloudmus-core: everything below the view — backends, playback, state,
# settings, view models — with no Qt Widgets (see fronts/qt/AGENTS.md). Shared
# by the desktop front and the mobile one (fronts/mobile), which add this
# directory; so what only the desktop needs (hotkeys, updates, crash reports)
# is in cloudmus-desktop-core instead, and paths are absolute.
#
# Expects from the including project: C++20, AUTOMOC, the Qt6 Core, Gui and
# Network targets, and a cloudmus-mpv target.

get_filename_component(CLOUDMUS_QT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
if(NOT CLOUDMUS_REPO_ROOT)
    get_filename_component(CLOUDMUS_REPO_ROOT ${CLOUDMUS_QT_DIR}/../.. ABSOLUTE)
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
set(GENERATED_DIR ${CLOUDMUS_QT_DIR}/generated)
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

add_library(cloudmus-core STATIC
    ${CLOUDMUS_QT_DIR}/src/Rpc/NdjsonTransport.cpp
    ${CLOUDMUS_QT_DIR}/src/Rpc/BackendManifest.cpp
    ${CLOUDMUS_QT_DIR}/src/Rpc/RpcClient.cpp
    ${CLOUDMUS_QT_DIR}/src/Rpc/SourceManager.cpp
    ${CLOUDMUS_QT_DIR}/src/Rpc/AuthStates.cpp

    ${CLOUDMUS_QT_DIR}/src/Playback/AudioPlayer.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/AudioFileProbe.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/StreamRelay.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/NetworkPool.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/StreamDownload.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/TrackCache.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/Preloader.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/PlaybackController.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/BeatDetector.cpp
    ${CLOUDMUS_QT_DIR}/src/Playback/LevelFeed.cpp

    ${CLOUDMUS_QT_DIR}/src/Config/Paths.cpp
    ${CLOUDMUS_QT_DIR}/src/Config/Settings.cpp
    ${CLOUDMUS_QT_DIR}/src/I18n/Languages.cpp
    ${CLOUDMUS_QT_DIR}/src/I18n/Translator.cpp
    ${CLOUDMUS_QT_DIR}/src/Net/ProxyRouting.cpp
    ${CLOUDMUS_QT_DIR}/src/Net/NetworkErrors.cpp
    ${CLOUDMUS_QT_DIR}/src/Net/ParallelDownload.cpp

    ${CLOUDMUS_QT_DIR}/src/History/PlaybackHistory.cpp
    ${CLOUDMUS_QT_DIR}/src/Library/TrackStates.cpp
    ${CLOUDMUS_QT_DIR}/src/Library/DownloadPaths.cpp
    ${CLOUDMUS_QT_DIR}/src/Library/TrackFile.cpp

    ${CLOUDMUS_QT_DIR}/src/Covers/CoverArtCache.cpp

    ${CLOUDMUS_QT_DIR}/src/App/Core.cpp
    ${CLOUDMUS_QT_DIR}/src/App/Analytics.cpp
    ${CLOUDMUS_QT_DIR}/src/App/SourceSession.cpp
    ${CLOUDMUS_QT_DIR}/src/App/PlaylistEditing.cpp
    ${CLOUDMUS_QT_DIR}/src/App/TrackFetch.cpp
    ${CLOUDMUS_QT_DIR}/src/App/ListeningTime.cpp
    ${CLOUDMUS_QT_DIR}/src/App/Connectivity.cpp


    ${CLOUDMUS_QT_DIR}/src/ViewModel/AudioPulse.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/Messages.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/NowPlaying.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/SidebarModel.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/Sources.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/ActivePlaylist.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/ActiveTracks.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/OpenPlaylist.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/TrackActions.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/TrackFilterModel.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/SourcePage.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/SourceSettings.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/Downloads.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/Preload.cpp
    ${CLOUDMUS_QT_DIR}/src/ViewModel/TrackListModel.cpp


    ${CLOUDMUS_QT_DIR}/src/Timing/Debounce.cpp
    ${CLOUDMUS_QT_DIR}/src/Timing/Throttle.cpp

    ${GENERATED_DIR}/RpcMethods.cpp
)
add_dependencies(cloudmus-core protocol_codegen_cpp)

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
    ${CLOUDMUS_QT_DIR}/src/Rpc
    ${CLOUDMUS_QT_DIR}/src/Playback
    ${CLOUDMUS_QT_DIR}/src/Config
    ${CLOUDMUS_QT_DIR}/src/I18n
    ${CLOUDMUS_QT_DIR}/src/Net
    ${CLOUDMUS_QT_DIR}/src/History
    ${CLOUDMUS_QT_DIR}/src/Library
    ${CLOUDMUS_QT_DIR}/src/Covers
    ${CLOUDMUS_QT_DIR}/src/App
    ${CLOUDMUS_QT_DIR}/src/ViewModel
    ${CLOUDMUS_QT_DIR}/src/Timing
)

target_link_libraries(cloudmus-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    cloudmus-mpv
)

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
