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

/// Return the display string for a MachineType value.
inline const char* machine_type_str(MachineType t) {
    switch (t) {
        case MachineType::ZXN_ISSUE2: return "ZX Next";
        case MachineType::ZX48K:      return "48K";
        case MachineType::ZX128K:     return "128K";
        case MachineType::ZX_PLUS3:   return "+3";
    }
    return "unknown";
}

/// Parse a machine type string (case-insensitive). Returns true on success.
inline bool parse_machine_type(const std::string& s, MachineType& out) {
    std::string lower = s;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "48k" || lower == "48")         { out = MachineType::ZX48K;      return true; }
    if (lower == "128k" || lower == "128")       { out = MachineType::ZX128K;     return true; }
    if (lower == "+3" || lower == "plus3" || lower == "p3") { out = MachineType::ZX_PLUS3; return true; }
    if (lower == "next" || lower == "zxn")        { out = MachineType::ZXN_ISSUE2; return true; }
    return false;
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

    // SD card image. Wave 0.3 (Task 8 Multiface plan, 2026-05-04) made this
    // the canonical source for ALL non-embedded ROMs (DivMMC, NextZXOS,
    // 48K/128K/+3 BASIC, Multiface). Loaded via host-side FAT32 reader at
    // init time (src/core/sd_rom_extractor.{h,cpp}); the in-emulator SPI/SD
    // path is independent and serves Z80 software at runtime. Empty leaves
    // jnext with no peripheral or machine ROMs (legitimate for hermetic
    // unit-test fixtures; not allowed at the CLI level — main.cpp errors
    // out when --sd-card is missing).
    std::string sd_card_image;            // path to SD card .img file (empty = no SD)

    // Rewind buffer: number of frame snapshots to keep (0 = disabled)
    int rewind_buffer_frames = 500;

    // Magic breakpoint: ED FF (ZEsarUX) and DD 01 (CSpect) trigger debugger pause
    bool magic_breakpoint = false;

    // esxdos shim. When true, jnext intercepts RST $08 (esxdos entry) at
    // PC=$0008 and returns benign errors for known function codes (F_OPEN
    // returns ENOENT, F_CLOSE returns success, etc.). Lets z88dk-built
    // NEX games that depend on esxdos boot under --load (which bypasses
    // NextZXOS, so the real esxdos firmware isn't in DivMMC SRAM).
    bool esxdos_stub = false;

    // Task 18 firmware-bypass route. When true, jnext skips loading the
    // FPGA boot ROM overlay (tbblue.fw IPL) and instead populates SRAM
    // pages 0..7 directly from `/MACHINES/NEXT/enNextZX.rom` on the SD
    // image, then commits +3 machine type via NR 0x03=0xB3 — replicating
    // what `boot.c` would have done after parsing menu.def's default
    // entry. Z80 starts at PC=0x0000 with the NextZXOS ROM already in
    // place. Sidesteps the layer-7 wrong-roms/nextboot.rom issue and the
    // SPACEBAR menu UX. Implementation in src/core/emulator.cpp::init().
    bool bypass_tbblue_fw = false;

    // Magic port: debug output port that logs bytes to stderr
    bool     magic_port_enabled = false;
    uint16_t magic_port_address = 0x0000;  // 16-bit port address (default disabled)
    enum class MagicPortMode : uint8_t { HEX, DEC, ASCII, LINE };
    MagicPortMode magic_port_mode = MagicPortMode::HEX;

    // Compositor per-pixel trace (debug). When path is non-empty, the
    // renderer dumps one CSV row per pixel for the target frame to that
    // file. Used for compositor-fidelity investigations (e.g. parallax.nex).
    std::string compositor_trace_path;
    int         compositor_trace_frame = 250;

    // Task 21 (2026-05-29) — per-physical-address T-state profiler. When
    // `profile` is true, allocate ~16 MB of mmap'd histogram in
    // Emulator::init() and accumulate one entry per executed instruction
    // (gated branch in the CPU hot path: zero cost when profile=false).
    // On shutdown the histogram is written to `profile_output_path` as a
    // sorted text file consumable by `tools/get-function-heatmap.pl`.
    bool        profile             = false;
    std::string profile_output_path = "profile.dat";
};

// ---------------------------------------------------------------------------
// Machine-wide timing constants
// ---------------------------------------------------------------------------

/// Master clock frequency (28 MHz).
static constexpr uint64_t MASTER_CLOCK_HZ = 28'000'000ULL;

/// Per-machine timing parameters derived from VHDL (zxula_timing.vhd).
///
/// The VHDL defines pixel-clock (7 MHz) counters per machine type.
/// Each 7 MHz pixel tick = 4 master clock cycles (28 MHz).
/// Each CPU T-state at 3.5 MHz = 2 pixel ticks = 8 master cycles.
struct MachineTiming {
    int      pixels_per_line;       // 7 MHz pixel ticks per scanline (c_max_hc + 1)
    int      lines_per_frame;       // total scanlines per frame (c_max_vc + 1)
    int      tstates_per_line;      // pixels_per_line / 2
    int      tstates_per_frame;     // tstates_per_line * lines_per_frame
    uint64_t master_cycles_per_line;  // pixels_per_line * 4
    uint64_t master_cycles_per_frame; // master_cycles_per_line * lines_per_frame
};

/// Return timing constants for the given machine type.
/// Values from ZX Spectrum Next FPGA VHDL (zxula_timing.vhd).
inline constexpr MachineTiming machine_timing(MachineType type)
{
    switch (type) {
        case MachineType::ZX48K:
            // 48K PAL: c_max_hc=447 (448 pixels), c_max_vc=311 (312 lines)
            return {448, 312, 224, 224*312, 1792, 1792ULL*312};
        case MachineType::ZX128K:
        case MachineType::ZX_PLUS3:
            // 128K/+3 PAL: c_max_hc=455 (456 pixels), c_max_vc=310 (311 lines)
            return {456, 311, 228, 228*311, 1824, 1824ULL*311};
        case MachineType::ZXN_ISSUE2:
        default:
            // ZX Next defaults to 128K timing
            return {456, 311, 228, 228*311, 1824, 1824ULL*311};
    }
}

// Legacy global constants — kept for code that doesn't yet use MachineTiming.
// These match 128K/Next timing (the most common default).
static constexpr int FRAME_RATE_HZ = 50;
static constexpr uint64_t MASTER_CYCLES_PER_FRAME = 1824ULL * 311;   // 567,264
static constexpr int LINES_PER_FRAME = 311;
static constexpr uint64_t MASTER_CYCLES_PER_LINE = 1824;
static constexpr int TSTATES_PER_LINE = 228;
static constexpr int TSTATES_PER_FRAME = 228 * 311;
// = 72 960
