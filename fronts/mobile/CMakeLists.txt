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

find_package(Qt6 6.7 REQUIRED COMPONENTS Concurrent Core Network RemoteObjects Gui Qml Quick QuickControls2 Svg LinguistTools ShaderTools)
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

set(_shared_core_default ON)
option(CLOUDMUS_MOBILE_SHARED_CORE "Run App::Core rather than the dev tracks" ${_shared_core_default})

# --- The language: the desktop front's I18n::Translator, which both processes
# run (the views' own, and the core's). Where the core is in the binary
# already (the desktop) it has it; the UI process on Android has none, so
# builds the two files itself.
if(ANDROID OR NOT CLOUDMUS_MOBILE_SHARED_CORE)
    add_library(cloudmus-mobile-i18n STATIC
        ${QT_FRONT_SRC}/I18n/Languages.cpp
        ${QT_FRONT_SRC}/I18n/Languages.h
        ${QT_FRONT_SRC}/I18n/Translator.cpp
        ${QT_FRONT_SRC}/I18n/Translator.h
    )
    target_include_directories(cloudmus-mobile-i18n PUBLIC ${QT_FRONT_SRC}/I18n)
    target_link_libraries(cloudmus-mobile-i18n PUBLIC Qt6::Core)
    set_target_properties(cloudmus-mobile-i18n PROPERTIES POSITION_INDEPENDENT_CODE ON)
else()
    add_library(cloudmus-mobile-i18n INTERFACE)
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

# --- The core: App::Core, the desktop front's own, with the real backends. Off,
# src/DevTracks stands in for it, on Qt Core and Network alone.

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
        src/ViewModel/DownloadsScreen.cpp
        src/ViewModel/DownloadsScreen.h
        src/ViewModel/MainScreen.cpp
        src/ViewModel/MainScreen.h
        src/ViewModel/MediaTree.cpp
        src/ViewModel/MediaTree.h
        src/ViewModel/PlaylistScreen.cpp
        src/ViewModel/PlaylistScreen.h
        src/ViewModel/TrackScreen.cpp
        src/ViewModel/TrackScreen.h
        src/ViewModel/RowsModel.cpp
        src/ViewModel/RowsModel.h
        src/ViewModel/SettingsScreen.cpp
        src/ViewModel/SettingsScreen.h
        src/ViewModel/SourceSettingsScreen.cpp
        src/ViewModel/SourceSettingsScreen.h
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

# QR codes for signing in: one source file, no build of its own.
include(FetchContent)
FetchContent_Declare(qrcodegen
    URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
    URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(qrcodegen)

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
    src/Ui/Localizer.cpp
    src/Ui/Localizer.h
    src/Ui/Platform.cpp
    src/Ui/Platform.h
    src/Ui/QrImageProvider.cpp
    src/Ui/QrImageProvider.h
    ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp
)
target_include_directories(cloudmus-mobile-ui PRIVATE ${qrcodegen_SOURCE_DIR}/cpp)
target_include_directories(cloudmus-mobile-ui PUBLIC src/Ui src/Remote ${QT_FRONT_SRC}/Covers)
target_link_libraries(cloudmus-mobile-ui PUBLIC
    cloudmus-mobile-i18n cloudmus-mobile-log Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::RemoteObjects Qt6::Svg)
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
        qml/AppSlider.qml
        qml/AppSwitch.qml
        qml/AuraBackground.qml
        qml/BodyText.qml
        qml/CapabilityTags.qml
        qml/ConfirmDialog.qml
        qml/DropMenu.qml
        qml/DropMenuItem.qml
        qml/Chip.qml
        qml/Cover.qml
        qml/CrossfadeImage.qml
        qml/DeveloperPage.qml
        qml/FpsOverlay.qml
        qml/EqualizerBars.qml
        qml/IconButton.qml
        qml/InputField.qml
        qml/LogPage.qml
        qml/Main.qml
        qml/MarqueeText.qml
        qml/OfflineBadge.qml
        qml/PlaylistPage.qml
        qml/PreloadCells.qml
        qml/Ripple.qml
        qml/ProxyPage.qml
        qml/SettingField.qml
        qml/SettingStepper.qml
        qml/SettingSwitch.qml
        qml/SettingText.qml
        qml/SettingsCell.qml
        qml/SettingsGroup.qml
        qml/SettingsRow.qml
        qml/SourceSettingsPage.qml
        qml/Stepper.qml
        qml/Spinner.qml
        qml/TrackSheet.qml
        qml/TrackTitleBlock.qml
        qml/NowPlayingPage.qml
        qml/PageBar.qml
        qml/LibraryPage.qml
        qml/MainPage.qml
        qml/TrackPanel.qml
        qml/TrackRow.qml
        qml/ScrollArea.qml
        qml/ScrollEdgeFade.qml
        qml/ScrollList.qml
        qml/SectionHeading.qml
        qml/SeekBar.qml
        qml/SourceBar.qml
        qml/SourceLogo.qml
        qml/SettingsPage.qml
        qml/ShadowText.qml
        qml/TextButton.qml
        qml/TextCursor.qml
        qml/Theme.qml
        qml/Toast.qml
    SOURCES
        src/Ui/LogModel.cpp
        src/Ui/LogModel.h
)

# Shaders of the views' own, built to .qsb for every graphics API; ripple.frag
# is the touch feedback (Ripple.qml), at :/shaders/ripple.frag.qsb.
qt_add_shaders(cloudmus-mobile-ui "shaders"
    PREFIX "/"
    FILES shaders/ripple.frag)

set(_icons arrow_back check chevron_right close cloud_off content_copy delete expand_less expand_more favorite favorite_border file_download heart_off
    heart_off_outline history more_vert music_note open_in_new pause play_arrow playlist_add playlist_play
    radio refresh repeat repeat_one search shuffle skip_next skip_previous star star_border tune
    visibility visibility_off warning logout)
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

# The texts: one translations/cloudmus_<code>.ts per language (English is the
# source language), the views' own and the core's — the desktop's texts the
# shared core carries come along. .qm files embedded under :/i18n, where
# I18n::Translator looks; in the service's library too, which is another
# process on Android. After changing a text:
# `cmake --build build --target update_translations`.
set(_ts_files)
foreach(_lang be de es fr it ru)
    list(APPEND _ts_files ${CMAKE_CURRENT_SOURCE_DIR}/translations/cloudmus_${_lang}.ts)
endforeach()
set(_tr_targets cloudmus-mobile-ui cloudmus-mobile-core)
if(CLOUDMUS_MOBILE_SHARED_CORE)
    list(APPEND _tr_targets cloudmus-core)
endif()
qt_add_lupdate(TARGET update_translations
    TS_FILES ${_ts_files}
    SOURCE_TARGETS ${_tr_targets}
    OPTIONS -no-obsolete -locations none -source-language en)
qt_add_lrelease(TARGET cloudmus-mobile-ui
    TS_FILES ${_ts_files}
    QM_FILES_OUTPUT_VARIABLE _qm_files)
qt_add_resources(cloudmus-mobile-ui "i18n" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${_qm_files})

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
    qt_add_resources(cloudmus-mobile-service "i18n" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${_qm_files})
    # Plain names: CorePaths.java loads it with System.loadLibrary().
    set_target_properties(cloudmus-mobile-service PROPERTIES OUTPUT_NAME cloudmus-core)

    set(_mpv_libs)
    foreach(_lib mpv avcodec avdevice avfilter avformat avutil postproc swresample swscale)
        list(APPEND _mpv_libs "${CLOUDMUS_MPV_LIBS_DIR}/lib${_lib}.so")
    endforeach()

    # The package source: the android/ files, copied (and again whenever one
    # changes) into the build directory.
    set(_package ${CMAKE_CURRENT_BINARY_DIR}/android-package)
    file(GLOB_RECURSE _package_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/android
        ${CMAKE_CURRENT_SOURCE_DIR}/android/*)
    foreach(_file IN LISTS _package_files)
        configure_file(android/${_file} ${_package}/${_file} COPYONLY)
    endforeach()
    set(CLOUDMUS_LOCAL_ANDROID_BINARY "" CACHE FILEPATH "Rust backend executable for this Android ABI")
    if(CLOUDMUS_LOCAL_ANDROID_BINARY)
        add_custom_target(cloudmus-local-package
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_package}/libs/${ANDROID_ABI}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CLOUDMUS_LOCAL_ANDROID_BINARY} ${_package}/libs/${ANDROID_ABI}/libcmlocalfolder.so)
        add_dependencies(cloudmus-mobile cloudmus-local-package)
        configure_file(../../backends/local-folder/manifest.json ${_package}/assets/local-folder.json COPYONLY)
        configure_file(../../backends/local-folder/icon.svg ${_package}/assets/local-folder.svg COPYONLY)
    endif()
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
        # Every build, not only after a link: the package directory can be new.
        add_custom_target(cloudmus-python-package
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:cloudmus-python> ${_jni_libs}/libcmpython.so)
        add_dependencies(cloudmus-mobile cloudmus-python-package)
        foreach(_file python.zip python.stamp)
            configure_file(${CLOUDMUS_PYTHON_ANDROID_DIR}/${_file} ${_package}/assets/${_file} COPYONLY)
        endforeach()
    endif()

    # From the release tags, as the desktop's version is (fronts/qt/cmake/Version.cmake):
    # "0.6.4" on one, "0.6.4-163-geaef2c2" after it. The code only has to grow,
    # or Android won't take the update: x.y.z, then the commits since (a thousand
    # is more than there are between two releases).
    find_package(Git QUIET)
    set(CLOUDMUS_APK_VERSION ${PROJECT_VERSION})
    set(CLOUDMUS_APK_VERSION_CODE 1)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --match "[0-9]*"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE _described
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_described MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9]+)-g)?")
            set(CLOUDMUS_APK_VERSION ${_described})
            set(_commits 0)
            if(CMAKE_MATCH_5)
                set(_commits ${CMAKE_MATCH_5})
            endif()
            math(EXPR CLOUDMUS_APK_VERSION_CODE
                "${CMAKE_MATCH_1} * 10000000 + ${CMAKE_MATCH_2} * 100000 + ${CMAKE_MATCH_3} * 1000 + ${_commits}")
        endif()
    endif()
    message(STATUS "APK version ${CLOUDMUS_APK_VERSION}, code ${CLOUDMUS_APK_VERSION_CODE}")

    add_dependencies(cloudmus-mobile cloudmus-mobile-service)
    set_target_properties(cloudmus-mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${_package}
        QT_ANDROID_EXTRA_LIBS "$<TARGET_FILE:cloudmus-mobile-service>;${_mpv_libs}"
        QT_ANDROID_MIN_SDK_VERSION 26
        QT_ANDROID_TARGET_SDK_VERSION 34
        QT_ANDROID_VERSION_NAME ${CLOUDMUS_APK_VERSION}
        QT_ANDROID_VERSION_CODE ${CLOUDMUS_APK_VERSION_CODE}
    )
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
