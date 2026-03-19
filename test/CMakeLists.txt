find_package(GTest QUIET)
if(GTest_FOUND)
    file(GLOB_RECURSE TEST_SOURCES "*.cpp")
    if(TEST_SOURCES)
        add_executable(zxnext_tests ${TEST_SOURCES})
        target_link_libraries(zxnext_tests PRIVATE GTest::gtest_main
            zxnext_core zxnext_cpu zxnext_memory zxnext_video
            zxnext_audio zxnext_port zxnext_peripheral zxnext_input zxnext_save)
        target_include_directories(zxnext_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
        include(GoogleTest)
        gtest_discover_tests(zxnext_tests)
    endif()
endif()
