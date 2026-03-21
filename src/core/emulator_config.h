#pragma once

#include <cstdint>
#include <string>

// MachineType is the canonical shared enum; defined once in contention.h.
// All emulator modules that need the machine type include this header.
#include "memory/contention.h"

// ---------------------------------------------------------------------------
// Emulator configuration
// ---------------------------------------------------------------------------

/// CPU speed settings — matches NextREG 0x07 values 0–3.
enum class CpuSpeed : uint8_t {
    MHZ_3_5 = 0,   // 3.5 MHz (default)
    MHZ_7   = 1,   // 7 MHz
    MHZ_14  = 2,   // 14 MHz
    MHZ_28  = 3,   // 28 MHz
};

/// Return the display string for a CpuSpeed value (e.g. "3.5 MHz").
inline const char* cpu_speed_str(CpuSpeed s) {
    switch (s) {
        case CpuSpeed::MHZ_3_5: return "3.5 MHz";
        case CpuSpeed::MHZ_7:   return "7 MHz";
        case CpuSpeed::MHZ_14:  return "14 MHz";
        case CpuSpeed::MHZ_28:  return "28 MHz";
    }
    return "unknown";
}

/// Return the 28 MHz master clock divisor for a CpuSpeed value.
inline int cpu_speed_divisor(CpuSpeed s) {
    switch (s) {
        case CpuSpeed::MHZ_3_5: return 8;
        case CpuSpeed::MHZ_7:   return 4;
        case CpuSpeed::MHZ_14:  return 2;
        case CpuSpeed::MHZ_28:  return 1;
    }
    return 8;
}

struct EmulatorConfig {
    MachineType type        = MachineType::ZXN_ISSUE2;
    bool        turbo_sound = false;      // Enable TurboSound (3× AY chips)
    CpuSpeed    cpu_speed   = CpuSpeed::MHZ_3_5;

    // --inject: load a raw binary into RAM at a given address, then jump to it.
    std::string inject_file;              // path to raw binary (empty = disabled)
    uint16_t    inject_org  = 0x8000;     // load address (--inject-org)
    uint16_t    inject_pc   = 0x8000;     // entry point  (--inject-pc, default = inject_org)

    // --load: load a file (e.g. .nex) into the emulator.
    std::string load_file;                // path to .nex file (empty = disabled)

    // DivMMC / SD card
    std::string divmmc_rom_path;          // path to DivMMC ROM (empty = disabled)
    std::string sd_card_image;            // path to SD card .img file (empty = no SD)
};

// ---------------------------------------------------------------------------
// Machine-wide timing constants
// ---------------------------------------------------------------------------

/// Frame rate (Hz).
static constexpr int FRAME_RATE_HZ = 50;

/// Master clock frequency (28 MHz).
static constexpr uint64_t MASTER_CLOCK_HZ = 28'000'000ULL;

/// Master cycles per frame at 28 MHz.
static constexpr uint64_t MASTER_CYCLES_PER_FRAME = MASTER_CLOCK_HZ / FRAME_RATE_HZ;
// = 560 000 cycles/frame

/// Total scanlines per frame (including blanking).
/// ZX Spectrum Next (Issue 2): 320 lines total (312 visible + overscan).
static constexpr int LINES_PER_FRAME = 320;

/// Master clock cycles per scanline (28 MHz domain).
/// 28 000 000 / 50 / 320 = 1750 master cycles per line.
static constexpr uint64_t MASTER_CYCLES_PER_LINE = MASTER_CYCLES_PER_FRAME / LINES_PER_FRAME;
// = 1 750

/// T-states per scanline at 3.5 MHz (÷8 from 28 MHz).
/// 1750 / 8 = 218.75; the ZX Spectrum uses 228 T-states/line (standard).
/// 228 is the hardware value; use it directly for CPU accounting.
static constexpr int TSTATES_PER_LINE = 228;

/// T-states per frame at 3.5 MHz.
static constexpr int TSTATES_PER_FRAME = TSTATES_PER_LINE * LINES_PER_FRAME;
// = 72 960
