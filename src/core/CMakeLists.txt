file(GLOB_RECURSE SOURCES "*.cpp")

# ---------------------------------------------------------------------------
# Embed roms/nextboot.rom (8 KB FPGA bootloader) into the jnext binary.
#
# Mirrors the on-FPGA flash IPL of a real ZX Spectrum Next: silicon-baked,
# never on the SD card. See src/core/embedded_nextboot_rom.h for the
# accessor and Emulator::init for default-load behaviour. User decision
# 2026-05-04 (Task 8 Multiface plan, Wave 0.1).
#
# We generate a C byte array (embed_rom.cmake) rather than objcopy'ing the
# ROM into an object file. objcopy --input-target=binary is not portable:
# macOS ships no GNU objcopy that can emit a Mach-O object with the
# `_binary_*` symbols, and the arm64 macOS runner also rejects the
# i386:x86-64 arch flag. A generated C array compiles identically on Linux,
# Windows (MinGW) and macOS — and drops the CMAKE_OBJCOPY dependency entirely.
# The array is exposed via `nextboot_rom_data` / `nextboot_rom_size`
# (see embedded_nextboot_rom.h).
# ---------------------------------------------------------------------------
set(NEXTBOOT_ROM_SRC ${CMAKE_SOURCE_DIR}/roms/nextboot.rom)
set(NEXTBOOT_ROM_GEN ${CMAKE_CURRENT_BINARY_DIR}/nextboot_rom.c)
add_custom_command(
    OUTPUT  ${NEXTBOOT_ROM_GEN}
    COMMAND ${CMAKE_COMMAND}
            -DROM_IN=${NEXTBOOT_ROM_SRC}
            -DROM_OUT=${NEXTBOOT_ROM_GEN}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_rom.cmake
    DEPENDS ${NEXTBOOT_ROM_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/embed_rom.cmake
    COMMENT "Embedding nextboot.rom into jnext binary (portable C array)"
    VERBATIM
)
set_source_files_properties(${NEXTBOOT_ROM_GEN} PROPERTIES GENERATED TRUE)

# ---------------------------------------------------------------------------
# Vendored ChaN FatFs R0.15b (third_party/fatfs). Used by the SD-card
# provisioner to reformat a freshly downloaded image to a spec-valid FAT32
# (8 KB clusters) via f_mkfs + f_write, retiring the hand-rolled volume writer.
# Config lives in third_party/fatfs/ffconf.h. ff.c / ffunicode.c compile as C.
# ---------------------------------------------------------------------------
set(FATFS_DIR ${CMAKE_SOURCE_DIR}/third_party/fatfs)
set(FATFS_SOURCES ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffunicode.c)

add_library(jnext_core STATIC ${SOURCES} ${FATFS_SOURCES} ${NEXTBOOT_ROM_GEN})
# ${CMAKE_SOURCE_DIR} is on the path so that #include "third_party/fatfs/ff.h"
# resolves now that FatFs sits with the other vendored libraries at the root.
target_include_directories(jnext_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
# emulator.h includes input/keyboard.h; Keyboard symbols live in jnext_input.
# src/input is added before src/core in the root CMakeLists so this target exists.
target_link_libraries(jnext_core PUBLIC jnext_input jnext_peripheral jnext_debug jnext_profiler spdlog::spdlog zot_tzx ZLIB::ZLIB CURL::libcurl OpenSSL::Crypto)
