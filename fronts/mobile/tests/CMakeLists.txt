find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(cloudmus-mobile-tests
    TestMain.cpp
)
target_link_libraries(cloudmus-mobile-tests PRIVATE cloudmus-mobile-ui cloudmus-mobile-uiplugin Qt6::Test)
target_compile_definitions(cloudmus-mobile-tests PRIVATE
    CLOUDMUS_MOBILE_MEDIA_DIR="${CMAKE_SOURCE_DIR}/media")
add_test(NAME cloudmus-mobile-tests COMMAND cloudmus-mobile-tests)
set_tests_properties(cloudmus-mobile-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
