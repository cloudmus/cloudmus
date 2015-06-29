project(cloudmus)

cmake_minimum_required(VERSION 2.8)

option(CLOUDMUS_BUILD_WITH_QT5 "Build with Qt5 instead of Qt4" TRUE)
option(CLOUDMUS_USE_WEBENGINE "Build Cloudmus with WebEngine instead of WebKit" FALSE)

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

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

add_definitions(-std=c++11)

if(CLOUDMUS_BUILD_WITH_QT5)
    add_definitions(-DHAVE_QT5)
    if(CLOUDMUS_USE_WEBENGINE)
        add_definitions(-DCLOUDMUS_USE_WEBENGINE)
    endif()
endif()

add_subdirectory(app)

add_custom_target(root_files SOURCES
    cloudmus.astylerc
)
