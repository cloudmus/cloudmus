project(cloudmus)

macro(pre_build TARGET_NAME)
    add_custom_target( ${TARGET_NAME}_PRE_BUILD ALL )

    add_custom_command(
        TARGET ${TARGET_NAME}_PRE_BUILD
        ${ARGN}
        PRE_BUILD
        COMMENT "${TARGET_NAME}_PRE_BUILD ..."
    )

    add_custom_target(${TARGET_NAME}_CORE_PRE_BUILD)
    add_dependencies(${TARGET_NAME}_PRE_BUILD ${TARGET_NAME}_CORE_PRE_BUILD)
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_PRE_BUILD)
endmacro()

set(SOURCES
    cloudmus.astylerc
    main.cpp
    mainwindow.h
    mainwindow.cpp
    service_manager.h
    service_manager.cpp
    service_descriptor.h
    service_descriptor.cpp
    service.h
    service.cpp
    tools.h
)

set(UI
    mainwindow.ui
)

set(RESOURCES
    images.qrc
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5Widgets)
find_package(Qt5WebKitWidgets)

add_definitions(-std=c++11)
add_definitions(-DHAVE_QT5)

qt5_wrap_ui(SOURCES ${UI})
qt5_add_resources(SOURCES ${RESOURCES})

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_link_libraries(
    ${PROJECT_NAME}
    Qt5::Widgets
    Qt5::WebKitWidgets
)

pre_build(${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/services
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/services ${CMAKE_CURRENT_BINARY_DIR}/services
)

