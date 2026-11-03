cmake_minimum_required(VERSION 3.21)
project(cloudmus-mobile VERSION 0.1.0 LANGUAGES C CXX)

# Mobile front (MVP): QML views over a Qt Core-only core. On the desktop one
# binary holds both (or two processes with --split); on Android the core is
# a library of its own run by the playback service in its own process, the
# views talk to it over Qt Remote Objects. Kept to Qt 6.7: the newest that
# still runs on Android 8 (API 26).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Concurrent Core Network RemoteObjects Gui Qml Quick QuickControls2 Svg)
qt_standard_project_setup(REQUIRES 6.7)

# The desktop front's sources the core shares as they are.
set(QT_FRONT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../qt/src)
set(QT_FRONT_RES ${CMAKE_CURRENT_SOURCE_DIR}/../qt/resources)

# --- libmpv: the system's on the desktop; on Android the prebuilt one from
# the dev.jdtech.mpv:libmpv AAR, unpacked by build-android.sh with the
# mpv/client.h of its version.
add_library(cloudmus-mobile-mpv INTERFACE)
if(ANDROID)
    set(CLOUDMUS_MPV_ANDROID_DIR "" CACHE PATH "Unpacked libmpv AAR (jni/<abi>/*.so) plus include/")
    if(NOT EXISTS "${CLOUDMUS_MPV_ANDROID_DIR}/jni/${ANDROID_ABI}/libmpv.so")
        message(FATAL_ERROR "No libmpv for ${ANDROID_ABI} in CLOUDMUS_MPV_ANDROID_DIR (run build-android.sh)")
    endif()
    set(CLOUDMUS_MPV_LIBS_DIR "${CLOUDMUS_MPV_ANDROID_DIR}/jni/${ANDROID_ABI}")
    target_include_directories(cloudmus-mobile-mpv INTERFACE "${CLOUDMUS_MPV_ANDROID_DIR}/include")
    # libavcodec: ServiceMain hands FFmpeg the JavaVM (av_jni_set_java_vm).
    target_link_libraries(cloudmus-mobile-mpv INTERFACE
        "${CLOUDMUS_MPV_LIBS_DIR}/libmpv.so" "${CLOUDMUS_MPV_LIBS_DIR}/libavcodec.so")
    # libmpv's own dependencies sit next to it.
    target_link_options(cloudmus-mobile-mpv INTERFACE "-Wl,-rpath-link,${CLOUDMUS_MPV_LIBS_DIR}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    target_link_libraries(cloudmus-mobile-mpv INTERFACE PkgConfig::MPV)
endif()

# --- Logging: both processes, no Qt beyond Core.
add_library(cloudmus-mobile-log STATIC
    src/Logging/Log.cpp
    src/Logging/Log.h
)
target_include_directories(cloudmus-mobile-log PUBLIC src/Logging)
target_link_libraries(cloudmus-mobile-log PUBLIC Qt6::Core)
if(ANDROID)
    target_sources(cloudmus-mobile-log PRIVATE
        src/Android/Jni.cpp
        src/Android/Jni.h
        src/Logging/AndroidLog.cpp
        src/Logging/AndroidLog.h
    )
    target_include_directories(cloudmus-mobile-log PUBLIC src/Android)
    target_link_libraries(cloudmus-mobile-log PUBLIC log)
endif()
set_target_properties(cloudmus-mobile-log PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- The core: on the desktop App::Core, the desktop front's own, with the
# real backends. The APK can't run those yet (they are Python): it stays on
# src/DevTracks, a stand-in on Qt Core and Network alone, until it can.
if(ANDROID)
    set(_shared_core_default OFF)
else()
    set(_shared_core_default ON)
endif()
option(CLOUDMUS_MOBILE_SHARED_CORE "Run App::Core rather than the dev tracks" ${_shared_core_default})

add_library(cloudmus-mobile-core STATIC
    src/Remote/Host.cpp
    src/Remote/Host.h
    src/Remote/Names.h
    src/ViewModel/PlatformSettings.cpp
    src/ViewModel/PlatformSettings.h
)
target_include_directories(cloudmus-mobile-core PUBLIC src/Remote src/ViewModel)
target_link_libraries(cloudmus-mobile-core PUBLIC
    cloudmus-mobile-log cloudmus-mobile-mpv Qt6::Core Qt6::Network Qt6::RemoteObjects)
set_target_properties(cloudmus-mobile-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CLOUDMUS_MOBILE_SHARED_CORE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_library(cloudmus-mpv ALIAS cloudmus-mobile-mpv)
    add_subdirectory(../qt/core qt-core)
    target_sources(cloudmus-mobile-core PRIVATE
        src/App/MobileCore.cpp
        src/App/MobileCore.h
        src/ViewModel/LibraryScreen.cpp
        src/ViewModel/LibraryScreen.h
        src/ViewModel/RowsModel.cpp
        src/ViewModel/RowsModel.h
    )
    target_include_directories(cloudmus-mobile-core PUBLIC src/App)
    target_link_libraries(cloudmus-mobile-core PUBLIC cloudmus-core)
    target_compile_definitions(cloudmus-mobile-core PUBLIC CLOUDMUS_MOBILE_SHARED_CORE)
else()
    target_sources(cloudmus-mobile-core PRIVATE
        ${QT_FRONT_SRC}/Net/NetworkErrors.cpp
        ${QT_FRONT_SRC}/Net/ParallelDownload.cpp
        ${QT_FRONT_SRC}/Playback/AudioPlayer.cpp
        ${QT_FRONT_SRC}/Playback/LevelFeed.cpp
        ${QT_FRONT_SRC}/Playback/NetworkPool.cpp
        ${QT_FRONT_SRC}/Playback/StreamDownload.cpp
        ${QT_FRONT_SRC}/Playback/StreamRelay.cpp
        ${QT_FRONT_SRC}/Timing/Throttle.cpp
        src/DevTracks/ActivePlaylist.cpp
        src/DevTracks/ActivePlaylist.h
        src/DevTracks/MobileCore.cpp
        src/DevTracks/MobileCore.h
        src/DevTracks/NowPlaying.cpp
        src/DevTracks/NowPlaying.h
        src/DevTracks/QueuePlayer.cpp
        src/DevTracks/QueuePlayer.h
        src/DevTracks/Track.cpp
        src/DevTracks/Track.h
    )
    target_include_directories(cloudmus-mobile-core PUBLIC
        src/DevTracks ${QT_FRONT_SRC}/Net ${QT_FRONT_SRC}/Playback ${QT_FRONT_SRC}/Timing)
endif()

# --- The views: QML and what it binds to. A library of their own so the
# tests drive the same pages the app shows.
qt_add_library(cloudmus-mobile-ui STATIC
    ${QT_FRONT_SRC}/Covers/MeshAura.cpp
    src/Ui/AuraImageProvider.cpp
    src/Ui/AuraImageProvider.h
    src/Ui/Backend.cpp
    src/Ui/Backend.h
    src/Ui/CoverImageProvider.cpp
    src/Ui/CoverImageProvider.h
    src/Ui/Platform.cpp
    src/Ui/Platform.h
)
target_include_directories(cloudmus-mobile-ui PUBLIC src/Ui src/Remote ${QT_FRONT_SRC}/Covers)
target_link_libraries(cloudmus-mobile-ui PUBLIC
    cloudmus-mobile-log Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::RemoteObjects Qt6::Svg)
if(CLOUDMUS_MOBILE_SHARED_CORE)
    target_compile_definitions(cloudmus-mobile-ui PUBLIC CLOUDMUS_MOBILE_SHARED_CORE)
endif()
if(NOT ANDROID)
    # The desktop runs the core in the app; Android in the service.
    target_link_libraries(cloudmus-mobile-ui PUBLIC cloudmus-mobile-core)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(cloudmus-mobile-ui
    URI Cloudmus.Mobile
    VERSION 1.0
    QML_FILES
        qml/AuraBackground.qml
        qml/BodyText.qml
        qml/Chip.qml
        qml/Cover.qml
        qml/DeveloperPage.qml
        qml/FpsOverlay.qml
        qml/IconButton.qml
        qml/LogPage.qml
        qml/Main.qml
        qml/MiniPlayer.qml
        qml/NowPlayingPage.qml
        qml/PageBar.qml
        qml/LibraryPage.qml
        qml/MainPage.qml
        qml/TrackPanel.qml
        qml/TrackRow.qml
        qml/ScrollArea.qml
        qml/ScrollList.qml
        qml/SectionHeading.qml
        qml/SeekBar.qml
        qml/SettingsPage.qml
        qml/TextButton.qml
        qml/Theme.qml
        qml/Toast.qml
    SOURCES
        src/Ui/LogModel.cpp
        src/Ui/LogModel.h
)

set(_icons arrow_back close expand_less expand_more favorite favorite_border file_download heart_off
    heart_off_outline music_note pause play_arrow refresh skip_next skip_previous star star_border tune
    volume_up warning)
set(_icon_files)
foreach(_icon IN LISTS _icons)
    set(_file ${QT_FRONT_RES}/icons/symbols/${_icon}.svg)
    set_source_files_properties(${_file} PROPERTIES QT_RESOURCE_ALIAS ${_icon}.svg)
    list(APPEND _icon_files ${_file})
endforeach()
set_source_files_properties(${QT_FRONT_RES}/icons/logo.svg PROPERTIES QT_RESOURCE_ALIAS logo.svg)
qt_add_resources(cloudmus-mobile-ui "icons" PREFIX "/icons" FILES ${_icon_files} ${QT_FRONT_RES}/icons/logo.svg)
set(_fonts)
foreach(_weight Regular Medium SemiBold Bold)
    set(_file ${QT_FRONT_RES}/fonts/Manrope-${_weight}.ttf)
    set_source_files_properties(${_file} PROPERTIES QT_RESOURCE_ALIAS Manrope-${_weight}.ttf)
    list(APPEND _fonts ${_file})
endforeach()
qt_add_resources(cloudmus-mobile-ui "fonts" PREFIX "/fonts" FILES ${_fonts})

qt_add_executable(cloudmus-mobile src/Ui/main.cpp)
target_link_libraries(cloudmus-mobile PRIVATE cloudmus-mobile-ui cloudmus-mobile-uiplugin)
if(NOT ANDROID)
    target_compile_definitions(cloudmus-mobile PRIVATE
        CLOUDMUS_MOBILE_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
endif()

# --- Android: the playback service's library and the package.
if(ANDROID)
    add_library(cloudmus-mobile-service SHARED
        src/Android/ServiceMain.cpp
    )
    target_link_libraries(cloudmus-mobile-service PRIVATE cloudmus-mobile-core)
    # Plain names: CorePaths.java loads it with System.loadLibrary().
    set_target_properties(cloudmus-mobile-service PROPERTIES OUTPUT_NAME cloudmus-core)

    set(_mpv_libs)
    foreach(_lib mpv avcodec avdevice avfilter avformat avutil postproc swresample swscale)
        list(APPEND _mpv_libs "${CLOUDMUS_MPV_LIBS_DIR}/lib${_lib}.so")
    endforeach()

    # The package source: the android/ files plus the dev tracks as assets,
    # copied (and again whenever one changes) into the build directory.
    set(_package ${CMAKE_CURRENT_BINARY_DIR}/android-package)
    file(GLOB_RECURSE _package_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/android
        ${CMAKE_CURRENT_SOURCE_DIR}/android/*)
    foreach(_file IN LISTS _package_files)
        configure_file(android/${_file} ${_package}/${_file} COPYONLY)
    endforeach()
    file(GLOB _media CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/media/*)
    if(NOT _media)
        message(WARNING "No dev tracks in media/ (run tools/fetch-dev-tracks.sh): the APK will have none")
    endif()
    foreach(_file IN LISTS _media)
        get_filename_component(_name ${_file} NAME)
        configure_file(${_file} ${_package}/assets/media/${_name} COPYONLY)
    endforeach()

    # CPython and the backends (tools/bundle-python.sh), where there's a build
    # of it for the ABI.
    set(CLOUDMUS_PYTHON_ANDROID_DIR "" CACHE PATH "What tools/bundle-python.sh made for this ABI")
    if(CLOUDMUS_PYTHON_ANDROID_DIR)
        # In the package's jniLibs, not QT_ANDROID_EXTRA_LIBS: Qt loads those
        # into the UI process as it starts. Only Python's process needs these.
        set(_jni_libs ${_package}/libs/${ANDROID_ABI})
        file(GLOB _python_libs CONFIGURE_DEPENDS "${CLOUDMUS_PYTHON_ANDROID_DIR}/libs/*.so")
        file(GLOB _libpython "${CLOUDMUS_PYTHON_ANDROID_DIR}/libs/libpython3.*.so")
        file(GLOB _python_include "${CLOUDMUS_PYTHON_ANDROID_DIR}/include/python3.*")
        foreach(_lib IN LISTS _python_libs)
            get_filename_component(_name ${_lib} NAME)
            configure_file(${_lib} ${_jni_libs}/${_name} COPYONLY)
        endforeach()
        add_executable(cloudmus-python src/Android/PythonMain.c)
        target_include_directories(cloudmus-python PRIVATE ${_python_include})
        target_link_libraries(cloudmus-python PRIVATE ${_libpython})
        set_target_properties(cloudmus-python PROPERTIES OUTPUT_NAME libcmpython SUFFIX .so)
        add_custom_command(TARGET cloudmus-python POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cloudmus-python> ${_jni_libs}/libcmpython.so)
        add_dependencies(cloudmus-mobile cloudmus-python)
        foreach(_file python.zip python.stamp)
            configure_file(${CLOUDMUS_PYTHON_ANDROID_DIR}/${_file} ${_package}/assets/${_file} COPYONLY)
        endforeach()
    endif()

    add_dependencies(cloudmus-mobile cloudmus-mobile-service)
    set_target_properties(cloudmus-mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${_package}
        QT_ANDROID_EXTRA_LIBS "$<TARGET_FILE:cloudmus-mobile-service>;${_mpv_libs}"
        QT_ANDROID_MIN_SDK_VERSION 26
        QT_ANDROID_TARGET_SDK_VERSION 34
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE 1
    )
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
