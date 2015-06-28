set(TARGET qtsingleapplication)

set(SOURCES
    qtlocalpeer.h
    qtlocalpeer.cpp
    qtlockedfile.h
    qtlockedfile.cpp
    qtsingleapplication.h
    qtsingleapplication.cpp
    qtsinglecoreapplication.h
    qtsinglecoreapplication.cpp

)

if(UNIX)
set(SOURCES ${SOURCES}
    qtlockedfile_unix.cpp
)
elif(WIN32)
set(SOURCES ${SOURCES}
    qtlockedfile_win.cpp
)
endif()


if(CLOUDMUS_BUILD_WITH_QT5)
    find_package(Qt5Core)
    find_package(Qt5Widgets)
    find_package(Qt5Network)
else()
    find_package(Qt4 REQUIRED QtCore)
    include(${QT_USE_FILE})
endif()

add_executable(${TARGET} WIN32 ${SOURCES})

if(CLOUDMUS_BUILD_WITH_QT5)
    target_link_libraries(
        ${TARGET}
        Qt5::Core
        Qt5::Widgets
        Qt5::Network
    )
else()
    target_link_libraries(${TARGET} ${QT_LIBRARIES})
endif()
