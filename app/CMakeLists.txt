set(TARGET cloudmus)

cmake_policy(SET CMP0020 NEW)

set(SOURCES
    pointers.h
    finalizator.h
    finalizator.cpp
    scopedtools.h
    main.cpp
    application.h
    application.cpp
    mainwindow.h
    mainwindow.cpp
    service_manager.h
    service_manager.cpp
    service_descriptor.h
    service_descriptor.cpp
    service.h
    service.cpp
    tools.h
    autosaver.h
    autosaver.cpp
    cookiejar.h
    cookiejar.cpp
    optionsdialog.h
    optionsdialog.cpp
    aboutdialog.h
    aboutdialog.cpp
    optionspage.h
    optionspage.cpp
    options.h
    options.cpp
    applicationoptionspage.h
    applicationoptionspage.cpp
    shortcutsoptionspage.h
    shortcutsoptionspage.cpp
)

set(UI
    mainwindow.ui
    optionsdialog.ui
    aboutdialog.ui
    applicationoptionspage.ui
    shortcutsoptionspage.ui
)

set(RESOURCES
    images.qrc
)

add_subdirectory(3rdparty)

if(CLOUDMUS_BUILD_WITH_QT5)
    find_package(Qt5Widgets)
if(CLOUDMUS_USE_WEBENGINE)
    find_package(Qt5WebEngineWidgets)
else()
    find_package(Qt5WebKitWidgets)
endif()
    qt5_wrap_ui(SOURCES ${UI})
    qt5_add_resources(SOURCES ${RESOURCES})
else()
    find_package(Qt4 REQUIRED QtCore QtGui QtWebkit QtNetwork)
    include(${QT_USE_FILE})
    qt4_wrap_ui(SOURCES ${UI})
    qt4_add_resources(SOURCES ${RESOURCES})
endif()

add_executable(${TARGET} WIN32 ${SOURCES})

if(CLOUDMUS_BUILD_WITH_QT5)
    target_link_libraries(
        ${TARGET}
        Qt5::Widgets
    )

if(CLOUDMUS_USE_WEBENGINE)
    target_link_libraries(${TARGET} Qt5::WebEngineWidgets)
else()
    target_link_libraries(${TARGET} Qt5::WebKitWidgets)
endif()

else()
    target_link_libraries(${TARGET} ${QT_LIBRARIES})
endif()

target_link_libraries(${TARGET}
    qtsingleapplication
)

pre_build(${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/services
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/services ${CMAKE_CURRENT_BINARY_DIR}/services
)

