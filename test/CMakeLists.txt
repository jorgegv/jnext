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

# Memory/MMU compliance test
add_executable(mmu_test mmu/mmu_test.cpp)
target_link_libraries(mmu_test PRIVATE jnext_memory jnext_peripheral jnext_debug)
target_include_directories(mmu_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mmu_tests COMMAND mmu_test)

# NextREG register file compliance test
add_executable(nextreg_test nextreg/nextreg_test.cpp)
target_link_libraries(nextreg_test PRIVATE jnext_port)
target_include_directories(nextreg_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME nextreg_tests COMMAND nextreg_test)

# Input subsystem compliance test
add_executable(input_test input/input_test.cpp)
target_link_libraries(input_test PRIVATE jnext_input)
target_include_directories(input_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME input_tests COMMAND input_test)

# CTC compliance test
add_executable(ctc_test ctc/ctc_test.cpp)
target_link_libraries(ctc_test PRIVATE jnext_peripheral jnext_port)
target_include_directories(ctc_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ctc_tests COMMAND ctc_test)

# Layer 2 compliance test
add_executable(layer2_test layer2/layer2_test.cpp)
target_link_libraries(layer2_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(layer2_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME layer2_tests COMMAND layer2_test)

# UART + I2C/RTC compliance test
add_executable(uart_test uart/uart_test.cpp)
target_link_libraries(uart_test PRIVATE jnext_peripheral)
target_include_directories(uart_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME uart_tests COMMAND uart_test)

# DivMMC + SPI compliance test
add_executable(divmmc_test divmmc/divmmc_test.cpp)
target_link_libraries(divmmc_test PRIVATE jnext_peripheral)
target_include_directories(divmmc_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME divmmc_tests COMMAND divmmc_test)

# Sprites subsystem compliance test
add_executable(sprites_test sprites/sprites_test.cpp)
target_link_libraries(sprites_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(sprites_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME sprites_tests COMMAND sprites_test)

# Compositor compliance test
add_executable(compositor_test compositor/compositor_test.cpp)
target_link_libraries(compositor_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(compositor_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME compositor_tests COMMAND compositor_test)

# ULA video compliance test
add_executable(ula_test ula/ula_test.cpp)
target_link_libraries(ula_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(ula_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ula_tests COMMAND ula_test)

# I/O Port Dispatch compliance test
# Links the full emulator because the rewritten plan requires constructing a
# real Emulator (headless) and probing real peripheral handlers, not a
# synthetic clean container. See doc/design/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md.
add_executable(port_test port/port_test.cpp)
target_link_libraries(port_test PRIVATE
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(port_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME port_tests COMMAND port_test)

# Audio subsystem compliance test
add_executable(audio_test audio/audio_test.cpp)
target_link_libraries(audio_test PRIVATE jnext_audio)
target_include_directories(audio_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME audio_tests COMMAND audio_test)

# DMA subsystem compliance test
add_executable(dma_test dma/dma_test.cpp)
target_link_libraries(dma_test PRIVATE jnext_peripheral jnext_memory jnext_port)
target_include_directories(dma_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME dma_tests COMMAND dma_test)

# Tilemap subsystem compliance test
add_executable(tilemap_test tilemap/tilemap_test.cpp)
target_link_libraries(tilemap_test PRIVATE jnext_video jnext_memory jnext_port jnext_peripheral jnext_debug)
target_include_directories(tilemap_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tilemap_tests COMMAND tilemap_test)

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
