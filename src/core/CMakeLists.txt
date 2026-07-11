file(GLOB_RECURSE SOURCES "*.cpp")

# ---------------------------------------------------------------------------
# Embed roms/nextboot.rom (8 KB FPGA bootloader) into the jnext binary.
#
# Mirrors the on-FPGA flash IPL of a real ZX Spectrum Next: silicon-baked,
# never on the SD card. See src/core/embedded_nextboot_rom.h for the
# accessor and Emulator::init for default-load behaviour. User decision
# 2026-05-04 (Task 8 Multiface plan, Wave 0.1).
#
# objcopy --input-target=binary auto-generates three symbols:
#   _binary_roms_nextboot_rom_start
#   _binary_roms_nextboot_rom_end
#   _binary_roms_nextboot_rom_size
#
# We pass --output-target / --binary-architecture so the produced object
# matches the host ELF format. jnext currently only builds for Linux x86-64;
# if cross-compilation is added later, parameterise these with
# ${CMAKE_*_SYSTEM_PROCESSOR} or drop them and let objcopy infer.
# ---------------------------------------------------------------------------
set(NEXTBOOT_ROM_SRC ${CMAKE_SOURCE_DIR}/roms/nextboot.rom)
set(NEXTBOOT_ROM_OBJ ${CMAKE_CURRENT_BINARY_DIR}/nextboot_rom.o)

# objcopy derives the three `_binary_<path>_*` symbol names from the input
# path (each non-alphanumeric char becomes `_`). To get the clean
# `_binary_roms_nextboot_rom_*` names that embedded_nextboot_rom.h declares,
# we run objcopy with a relative input path from the source-tree root.
add_custom_command(
    OUTPUT  ${NEXTBOOT_ROM_OBJ}
    COMMAND ${CMAKE_OBJCOPY}
            --input-target=binary
            --output-target=elf64-x86-64
            --binary-architecture=i386:x86-64
            roms/nextboot.rom
            ${NEXTBOOT_ROM_OBJ}
    DEPENDS ${NEXTBOOT_ROM_SRC}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Embedding nextboot.rom into jnext binary"
    VERBATIM
)
set_source_files_properties(${NEXTBOOT_ROM_OBJ} PROPERTIES
    EXTERNAL_OBJECT TRUE
    GENERATED TRUE)

# ---------------------------------------------------------------------------
# Vendored ChaN FatFs R0.15b (src/third_party/fatfs). Used by the SD-card
# provisioner to reformat a freshly downloaded image to a spec-valid FAT32
# (8 KB clusters) via f_mkfs + f_write, retiring the hand-rolled volume writer.
# Config lives in third_party/fatfs/ffconf.h. ff.c / ffunicode.c compile as C.
# ---------------------------------------------------------------------------
set(FATFS_DIR ${CMAKE_SOURCE_DIR}/src/third_party/fatfs)
set(FATFS_SOURCES ${FATFS_DIR}/ff.c ${FATFS_DIR}/ffunicode.c)

add_library(jnext_core STATIC ${SOURCES} ${FATFS_SOURCES} ${NEXTBOOT_ROM_OBJ})
target_include_directories(jnext_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# emulator.h includes input/keyboard.h; Keyboard symbols live in jnext_input.
# src/input is added before src/core in the root CMakeLists so this target exists.
target_link_libraries(jnext_core PUBLIC jnext_input jnext_peripheral jnext_debug jnext_profiler spdlog::spdlog zot_tzx ZLIB::ZLIB CURL::libcurl)
