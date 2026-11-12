find_package(Qt6 REQUIRED COMPONENTS Test)

if(CLOUDMUS_MOBILE_SHARED_CORE)
    qt_add_executable(cloudmus-mobile-tests
        SharedCoreTest.cpp
        ../../qt/tests/FakeBackend.cpp
    )
    target_include_directories(cloudmus-mobile-tests PRIVATE ../../qt/tests)
else()
    # The dev tracks' own, until they are gone.
    qt_add_executable(cloudmus-mobile-tests
        TestMain.cpp
    )
    target_compile_definitions(cloudmus-mobile-tests PRIVATE
        CLOUDMUS_MOBILE_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
endif()
target_link_libraries(cloudmus-mobile-tests PRIVATE cloudmus-mobile-ui cloudmus-mobile-uiplugin Qt6::Test)
add_test(NAME cloudmus-mobile-tests COMMAND cloudmus-mobile-tests)
set_tests_properties(cloudmus-mobile-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
