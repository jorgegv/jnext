#pragma once

#include <cstddef>
#include <cstdint>

// FPGA boot ROM (8 KB) embedded into the jnext binary at link time.
//
// This mirrors the on-FPGA flash IPL of a real ZX Spectrum Next, which
// is silicon-baked and never touches the SD card. Embedding here means
// jnext is self-contained for the boot stage: no CLI flag and no SD
// lookup needed for the bootloader overlay at 0x0000-0x1FFF.
//
// User decision (2026-05-04, Task 8 Multiface plan, Wave 0.1 + 0.3):
// nextboot.rom is the only ROM that's silicon-baked; everything else
// comes from the user-supplied SD-card image (--sdcard, mandatory) via
// src/core/sd_rom_extractor.{h,cpp}.
//
// `nextboot_rom_data` / `nextboot_rom_size` are defined in a C source that
// src/core/CMakeLists.txt generates from roms/nextboot.rom via embed_rom.cmake
// (a portable byte array — see the comment there for why not objcopy).

extern "C" {
    extern const uint8_t nextboot_rom_data[];
    extern const unsigned nextboot_rom_size;
}

inline const uint8_t* embedded_nextboot_rom_data() {
    return nextboot_rom_data;
}

inline std::size_t embedded_nextboot_rom_size() {
    return static_cast<std::size_t>(nextboot_rom_size);
}
