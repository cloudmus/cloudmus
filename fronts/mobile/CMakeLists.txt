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

# The library both fronts share (the desktop's too): sources and resources.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
set(CORE_SRC ${CORE_DIR}/src)
set(CORE_RES ${CORE_DIR}/resources)

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

# BUILD_TESTING for the core's test support too (core/CMakeLists.txt).
include(CTest)

# --- cloudmus-common: the language (I18n::Translator), which both processes
# run (the views' own, and the core's), the About data and the cover aura.
# Built on its own: the UI process on Android has none of cloudmus-core.
add_subdirectory(${CORE_DIR}/common core-common)
add_library(cloudmus-mobile-i18n INTERFACE)
target_link_libraries(cloudmus-mobile-i18n INTERFACE cloudmus-common)

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

# --- The core: App::Core, the one both fronts share, with the real backends.

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

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(cloudmus-mpv ALIAS cloudmus-mobile-mpv)
add_subdirectory(${CORE_DIR} core)
target_sources(cloudmus-mobile-core PRIVATE
    src/App/MobileCore.cpp
    src/App/MobileCore.h
    src/ViewModel/LibraryScreen.cpp
    src/ViewModel/LibraryScreen.h
    src/ViewModel/DownloadsScreen.cpp
    src/ViewModel/DownloadsScreen.h
    src/ViewModel/ImitatedMusic.cpp
    src/ViewModel/ImitatedMusic.h
    src/ViewModel/MainScreen.cpp
    src/ViewModel/MainScreen.h
    src/ViewModel/MediaTree.cpp
    src/ViewModel/MediaTree.h
    src/ViewModel/PlaylistEntries.cpp
    src/ViewModel/PlaylistEntries.h
    src/ViewModel/PlaylistScreen.cpp
    src/ViewModel/PlaylistScreen.h
    src/ViewModel/PlaylistScreens.cpp
    src/ViewModel/PlaylistScreens.h
    src/ViewModel/PulseScreen.cpp
    src/ViewModel/PulseScreen.h
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

# QR codes for signing in: one source file, no build of its own.
include(FetchContent)
FetchContent_Declare(qrcodegen
    URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
    URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(qrcodegen)

# The version, from the release tags, as the desktop's is (fronts/desktop/cmake/Version.cmake):
# "0.6.4" on one, "0.6.4-163-geaef2c2" after it. The About page shows it; the
# APK is named by it. The APK's code only has to grow, or Android won't take
# the update: x.y.z, then the commits since (a thousand is more than there are
# between two releases).
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
message(STATUS "Version ${CLOUDMUS_APK_VERSION}, APK code ${CLOUDMUS_APK_VERSION_CODE}")

# --- The views: QML and what it binds to. A library of their own so the
# tests drive the same pages the app shows.
qt_add_library(cloudmus-mobile-ui STATIC
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
target_include_directories(cloudmus-mobile-ui PUBLIC src/Ui src/Remote)
target_link_libraries(cloudmus-mobile-ui PUBLIC
    cloudmus-common cloudmus-mobile-i18n cloudmus-mobile-log Qt6::Concurrent Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::RemoteObjects Qt6::Svg)
target_compile_definitions(cloudmus-mobile-ui PRIVATE CLOUDMUS_MOBILE_VERSION="${CLOUDMUS_APK_VERSION}")
# The core checks for updates against it (App::MobileCore).
target_compile_definitions(cloudmus-mobile-core PRIVATE CLOUDMUS_MOBILE_VERSION="${CLOUDMUS_APK_VERSION}")
if(NOT ANDROID)
    # The desktop runs the core in the app; Android in the service.
    target_link_libraries(cloudmus-mobile-ui PUBLIC cloudmus-mobile-core)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(cloudmus-mobile-ui
    URI Cloudmus.Mobile
    VERSION 1.0
    QML_FILES
        qml/AboutPage.qml
        qml/AppComboBox.qml
        qml/AppSlider.qml
        qml/AppSwitch.qml
        qml/AuraBackground.qml
        qml/BodyText.qml
        qml/CapabilityTags.qml
        qml/QrCode.qml
        qml/SignInCard.qml
        qml/CloudsBackdrop.qml
        qml/ConfirmDialog.qml
        qml/DropMenu.qml
        qml/DropMenuItem.qml
        qml/CheckMark.qml
        qml/Chip.qml
        qml/Cover.qml
        qml/CrossfadeImage.qml
        qml/DeveloperPage.qml
        qml/DownloadButton.qml
        qml/DownloadsPage.qml
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
        qml/PulsingLogo.qml
        qml/ProgressRing.qml
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
        qml/StartSplash.qml
        qml/TrackSheet.qml
        qml/AxisSwipe.qml
        qml/TrackTitleBlock.qml
        qml/NowPlayingPage.qml
        qml/PageBar.qml
        qml/PersonCard.qml
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
        qml/ToastStack.qml
        qml/OnboardingPage.qml
        qml/UpdatePage.qml
        qml/WelcomePanel.qml
    SOURCES
        src/Ui/LogModel.cpp
        src/Ui/LogModel.h
)

# Shaders of the views' own, built to .qsb for every graphics API; ripple.frag
# is the touch feedback (Ripple.qml), at :/shaders/ripple.frag.qsb.
qt_add_shaders(cloudmus-mobile-ui "shaders"
    PREFIX "/"
    FILES shaders/ripple.frag)

set(_icons smartphone folder album arrow_back check chevron_right close cloud_off content_copy delete expand_less expand_more favorite favorite_border file_download heart_off
    heart_off_outline history more_horiz more_vert music_note open_in_new pause play_arrow playlist_add playlist_play
    cloudmus person radio refresh repeat repeat_one search shuffle skip_next skip_previous star star_border tune
    visibility visibility_off warning logout volume_down volume_up upgrade info fullscreen)
set(_icon_files)
foreach(_icon IN LISTS _icons)
    set(_file ${CORE_RES}/icons/symbols/${_icon}.svg)
    set_source_files_properties(${_file} PROPERTIES QT_RESOURCE_ALIAS ${_icon}.svg)
    list(APPEND _icon_files ${_file})
endforeach()
set_source_files_properties(${CORE_RES}/icons/logo.svg PROPERTIES QT_RESOURCE_ALIAS logo.svg)
set_source_files_properties(${CORE_RES}/icons/cloud.svg PROPERTIES QT_RESOURCE_ALIAS cloud.svg)
qt_add_resources(cloudmus-mobile-ui "icons" PREFIX "/icons"
    FILES ${_icon_files} ${CORE_RES}/icons/logo.svg ${CORE_RES}/icons/cloud.svg)
# The About page's license text, people (Ui::Platform), technologies'
# logos and waving AI agents, as the desktop has them.
set(_about_files ../../LICENSE ${CORE_RES}/people.json
    ${CORE_RES}/agents/clawd-waving.png ${CORE_RES}/agents/codex-waving.png)
foreach(_logo qt mpv cpp rust python)
    list(APPEND _about_files ${CORE_RES}/technologies/${_logo}.svg)
endforeach()
foreach(_file IN LISTS _about_files)
    get_filename_component(_name ${_file} NAME)
    set_source_files_properties(${_file} PROPERTIES QT_RESOURCE_ALIAS ${_name})
endforeach()
qt_add_resources(cloudmus-mobile-ui "about" PREFIX "/about" FILES ${_about_files})
set(_fonts)
foreach(_weight Regular Medium SemiBold Bold)
    set(_file ${CORE_RES}/fonts/Manrope-${_weight}.ttf)
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
set(_tr_targets cloudmus-mobile-ui cloudmus-mobile-core cloudmus-core)
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

# --- Android: the playback service's library and the package.

# How the app gets its updates: "github" — the APK of the GitHub release,
# downloaded and installed by the app itself (REQUEST_INSTALL_PACKAGES);
# "store" — for Google Play and RuStore, which don't allow that: the store the
# app came from updates it (their in-app update libraries), and no permission.
set(CLOUDMUS_ANDROID_DISTRIBUTION "github" CACHE STRING "How the APK is updated: github or store")
set_property(CACHE CLOUDMUS_ANDROID_DISTRIBUTION PROPERTY STRINGS github store)
if(NOT CLOUDMUS_ANDROID_DISTRIBUTION MATCHES "^(github|store)$")
    message(FATAL_ERROR "CLOUDMUS_ANDROID_DISTRIBUTION must be github or store")
endif()
if(CLOUDMUS_ANDROID_DISTRIBUTION STREQUAL "store")
    target_compile_definitions(cloudmus-mobile-core PUBLIC CLOUDMUS_STORE_BUILD)
    target_compile_definitions(cloudmus-mobile-ui PUBLIC CLOUDMUS_STORE_BUILD)
endif()

function(cloudmus_write_if_changed path content)
    if(EXISTS "${path}")
        file(READ "${path}" _old)
        if(_old STREQUAL content)
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

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
    list(REMOVE_ITEM _package_files AndroidManifest.xml build.gradle)
    foreach(_file IN LISTS _package_files)
        configure_file(android/${_file} ${_package}/${_file} COPYONLY)
    endforeach()
    # The manifest and the Gradle script, with what the distribution adds
    # (see CLOUDMUS_ANDROID_DISTRIBUTION): written only when that changes, so
    # Gradle isn't run again for nothing.
    if(CLOUDMUS_ANDROID_DISTRIBUTION STREQUAL "store")
        set(_install_permission "")
        set(_store_repositories
            "maven { url \"https://nexus-external.rustore.ru/repository/maven-rustore-exposed\" }")
        set(_store_dependencies "implementation \"com.google.android.play:app-update:2.1.0\"
    implementation \"ru.rustore.sdk:appupdate:10.5.2\"")
        set(_updates_dir "updates-store")
    else()
        set(_install_permission
            "<uses-permission android:name=\"android.permission.REQUEST_INSTALL_PACKAGES\" />")
        set(_store_repositories "")
        set(_store_dependencies "")
        set(_updates_dir "updates-github")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidManifest.xml ${CMAKE_CURRENT_SOURCE_DIR}/android/build.gradle)
    file(READ android/AndroidManifest.xml _text)
    string(REPLACE "<!-- %%CLOUDMUS_INSTALL_PERMISSION%% -->" "${_install_permission}" _text "${_text}")
    cloudmus_write_if_changed(${_package}/AndroidManifest.xml "${_text}")
    file(READ android/build.gradle _text)
    string(REPLACE "// %%CLOUDMUS_STORE_REPOSITORIES%%" "${_store_repositories}" _text "${_text}")
    string(REPLACE "// %%CLOUDMUS_STORE_DEPENDENCIES%%" "${_store_dependencies}" _text "${_text}")
    string(REPLACE "'updates-github'" "'${_updates_dir}'" _text "${_text}")
    cloudmus_write_if_changed(${_package}/build.gradle "${_text}")
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

if(BUILD_TESTING AND NOT ANDROID)
    add_subdirectory(tests)
endif()
