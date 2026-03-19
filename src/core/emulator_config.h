#pragma once

#include <cstdint>

// MachineType is the canonical shared enum; defined once in contention.h.
// All emulator modules that need the machine type include this header.
#include "memory/contention.h"

// ---------------------------------------------------------------------------
// Emulator configuration
// ---------------------------------------------------------------------------

struct EmulatorConfig {
    MachineType type        = MachineType::ZXN_ISSUE2;
    bool        turbo_sound = false;   // Enable TurboSound (3× AY chips)
    int         cpu_speed_mhz = 4;    // Nominal CPU speed in MHz (3.5 → 4, 7, 14, 28)
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
