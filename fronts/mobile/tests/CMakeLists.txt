find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(cloudmus-mobile-tests
    SharedCoreTest.cpp
)
target_link_libraries(cloudmus-mobile-tests PRIVATE cloudmus-core-test-support cloudmus-mobile-ui cloudmus-mobile-uiplugin Qt6::Test)
add_test(NAME cloudmus-mobile-tests COMMAND cloudmus-mobile-tests)
set_tests_properties(cloudmus-mobile-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
