# FUSE Z80 opcode test runner (standalone, no GoogleTest required)
add_executable(fuse_z80_test fuse_z80_test.cpp)

# Rewind / backwards execution tests (standalone, no GoogleTest required)
add_executable(rewind_test rewind_test.cpp)
target_link_libraries(rewind_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(rewind_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME rewind_tests COMMAND rewind_test)
target_link_libraries(fuse_z80_test PRIVATE jnext_cpu)
target_include_directories(fuse_z80_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Copy test data files to build directory for convenience
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fuse/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/fuse/)

# Register as CTest target
add_test(NAME fuse_z80_tests COMMAND fuse_z80_test ${CMAKE_CURRENT_BINARY_DIR}/fuse)

# Z80N extended opcode test runner
add_executable(z80n_test z80n_test.cpp)
target_link_libraries(z80n_test PRIVATE jnext_cpu)
target_include_directories(z80n_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/z80n/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/z80n/)
add_test(NAME z80n_tests COMMAND z80n_test ${CMAKE_CURRENT_BINARY_DIR}/z80n)

# Copper coprocessor compliance test
add_executable(copper_test copper/copper_test.cpp)
target_link_libraries(copper_test PRIVATE jnext_peripheral jnext_port)
target_include_directories(copper_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME copper_tests COMMAND copper_test)

# GoogleTest-based tests (if available)
find_package(GTest QUIET)
if(GTest_FOUND)
    file(GLOB_RECURSE GTEST_SOURCES "test_*.cpp")
    if(GTEST_SOURCES)
        add_executable(jnext_tests ${GTEST_SOURCES})
        target_link_libraries(jnext_tests PRIVATE GTest::gtest_main
            jnext_core jnext_cpu jnext_memory jnext_video
            jnext_audio jnext_port jnext_peripheral jnext_input jnext_save)
        target_include_directories(jnext_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
        include(GoogleTest)
        gtest_discover_tests(jnext_tests)
    endif()
endif()
