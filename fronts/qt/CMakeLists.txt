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

set(CLOUDMUS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(CLOUDMUS_REPO_ROOT ${CLOUDMUS_REPO_ROOT} ABSOLUTE)

# --- cloudmus-core: shared with the mobile front.
add_subdirectory(core)

# --- cloudmus-desktop-core: what the core has only on the desktop — hotkeys,
# updates, crash reports. No Qt Widgets either.
add_library(cloudmus-desktop-core STATIC
    src/App/DesktopCore.cpp
    src/ViewModel/Browse.cpp
    src/App/SingleInstance.cpp
    src/App/StarPrompt.cpp

    src/Playback/CuePlayer.cpp

    src/Hotkeys/Actions.cpp
    src/Hotkeys/Registry.cpp
    src/Hotkeys/Dispatcher.cpp

    src/Diagnostics/CrashReporter.cpp
    src/Diagnostics/CrashShortcut.cpp

    src/Update/ReleaseFeed.cpp
    src/Update/UpdateChecker.cpp
    src/Update/UpdateDownloader.cpp
)
if(WIN32)
    target_sources(cloudmus-desktop-core PRIVATE src/Diagnostics/CrashReporterWin.cpp src/Update/InstallerWin.cpp)
elseif(APPLE)
    target_sources(cloudmus-desktop-core PRIVATE src/Diagnostics/CrashReporterPosix.cpp src/Update/InstallerMac.cpp)
else()
    target_sources(cloudmus-desktop-core PRIVATE src/Diagnostics/CrashReporterPosix.cpp src/Update/InstallerLinux.cpp)
endif()
target_include_directories(cloudmus-desktop-core PUBLIC
    src/Hotkeys
    src/Diagnostics
    src/Update
)
target_link_libraries(cloudmus-desktop-core PUBLIC cloudmus-core)

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
    target_link_libraries(cloudmus-desktop-core PUBLIC sentry::sentry)
    target_compile_definitions(cloudmus-desktop-core PUBLIC CLOUDMUS_HAS_SENTRY)
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
    src/Covers/MeshAura.cpp
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
    cloudmus-desktop-core
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
    target_precompile_headers(cloudmus-desktop-core PRIVATE ${_pch_core})
    target_precompile_headers(cloudmus-ui PRIVATE ${_pch_core} ${_pch_ui})
    if(CLOUDMUS_CCACHE_PROGRAM AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Keeps the PCH's bytes independent of when it was written.
        target_compile_options(cloudmus-core PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
        target_compile_options(cloudmus-desktop-core PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
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
    # CoCreateInstance(ITaskbarList3) (TaskbarThumbButtons.cpp) and the Start
    # Menu shortcut's AppUserModelID (AppIdentityWin.cpp).
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
# Linux only: a Mac may have xcb or wayland-client from Homebrew (GitHub's
# runners do), and macOS's Qt has no X11 or Wayland interfaces.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS GuiPrivate)
if(UNIX AND NOT APPLE)
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
if(UNIX AND NOT APPLE)
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
# InstallerLinux.cpp on Windows, InstallerMac.cpp on both) along with the
# translations. Only those — a file given twice confuses lupdate's
# namespaces.
file(GLOB_RECURSE _tr_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
foreach(_target cloudmus-core cloudmus-desktop-core cloudmus-ui cloudmus-qt)
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
    SOURCE_TARGETS cloudmus-core cloudmus-desktop-core cloudmus-ui cloudmus-qt
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
# Apple's linker has no --build-id: it always writes an LC_UUID instead.
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
    tests/RemotingTest.cpp
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
    tests/SingleInstanceTest.cpp
)
target_include_directories(cloudmus-qt-tests PRIVATE tests)
target_link_libraries(cloudmus-qt-tests PRIVATE cloudmus-desktop-core Qt6::Test)
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
