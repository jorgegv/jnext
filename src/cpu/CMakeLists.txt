file(GLOB_RECURSE SOURCES "*.cpp")

# FUSE Z80 core — single C file that #includes the FUSE opcode files
set(FUSE_Z80_DIR "${CMAKE_SOURCE_DIR}/third_party/fuse-z80")
set(FUSE_Z80_SOURCES "${FUSE_Z80_DIR}/fuse_z80_core.c")

add_library(jnext_cpu STATIC ${SOURCES} ${FUSE_Z80_SOURCES})

target_include_directories(jnext_cpu PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FUSE_Z80_DIR})
target_link_libraries(jnext_cpu PUBLIC spdlog::spdlog)
