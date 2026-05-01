file(GLOB_RECURSE SOURCES "*.cpp")

# FUSE Z80 core — single C file that #includes the FUSE opcode files
set(FUSE_Z80_DIR "${CMAKE_SOURCE_DIR}/third_party/fuse-z80")
set(FUSE_Z80_SOURCES "${FUSE_Z80_DIR}/fuse_z80_core.c")

add_library(jnext_cpu STATIC ${SOURCES} ${FUSE_Z80_SOURCES})

# G141 (2026-05-01) — FUSE Z80 in-opcode contention rewires through
# ContentionModel::contention_tick(). The FUSE `contend_read[_no_mreq]`
# / `contend_write_no_mreq` macros at z80_macros.h:107-130 expose a
# CORETEST-gated function-override path; defining CORETEST for the
# FUSE source file forces the macros to become extern function calls,
# which we implement in z80_cpu.cpp by routing each call through the
# VHDL-faithful per-cycle gate. Scoped to this TU only so the rest of
# the codebase is untouched.
set_source_files_properties(${FUSE_Z80_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "CORETEST")

target_include_directories(jnext_cpu PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FUSE_Z80_DIR})
# z80_cpu.cpp references ContentionModel and Mmu (Phase-2 contention
# wiring 2026-04-26). PUBLIC-link jnext_memory so callers (incl. test
# binaries) resolve those symbols transitively. Static-library cycle is
# benign — the linker resolves both archives in a single pass.
target_link_libraries(jnext_cpu PUBLIC spdlog::spdlog jnext_memory)
