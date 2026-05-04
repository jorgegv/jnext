#pragma once

#include "memory/mmu.h"

#include <cstdint>
#include <string>
#include <vector>

class Emulator;

/// Parsed NEX file header (V1.0 / V1.1 / V1.2).
struct NexHeader {
    char     magic[4];           // "Next"
    char     version[4];         // "V1.0", "V1.1", or "V1.2"
    // ram_required: NEX V1.1+ minimum-RAM hint at header offset 8.
    // Canonical spec (https://wiki.specnext.dev/NEX_file_format) defines
    // only values 0 = 768 KB and 1 = 1792 KB; value 2 = 2048 KB is in
    // working use (see KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md G155). Any
    // other value is treated as unknown by `NexLoader::ram_required_kb`.
    uint8_t  ram_required;
    uint8_t  num_banks;          // number of 16KB banks in file
    uint8_t  screen_flags;       // bit flags for screen data
    uint8_t  border_colour;      // 0-7
    uint16_t sp;                 // stack pointer
    uint16_t pc;                 // program counter (0 = don't execute)
    uint16_t extra_files;        // unused
    uint8_t  banks[112];         // banks[i]==1 means bank i is present
    uint8_t  loading_bar;        // 0=OFF, 1=ON
    uint8_t  loading_bar_colour;
    uint8_t  loading_delay;
    uint8_t  start_delay;
    uint8_t  preserve_regs;      // 0=reset, 1=preserve
    uint8_t  core_version[3];    // major, minor, subminor
    uint8_t  hires_colour;       // HiRes colour or L2 palette offset
    uint8_t  entry_bank;         // 16K bank mapped to 0xC000 at entry
    uint16_t file_handle;        // 0=close, 1=keep open

    // Screen flag bit masks
    static constexpr uint8_t SCREEN_LAYER2    = 0x01;
    static constexpr uint8_t SCREEN_ULA       = 0x02;
    static constexpr uint8_t SCREEN_LORES     = 0x04;
    static constexpr uint8_t SCREEN_HIRES     = 0x08;
    static constexpr uint8_t SCREEN_HICOLOUR  = 0x10;
    static constexpr uint8_t SCREEN_NO_PAL    = 0x80;
};

/// NEX file loader for the ZX Spectrum Next emulator.
///
/// Usage:
///   NexLoader loader;
///   if (loader.load("game.nex")) {
///       loader.apply(emulator);
///   }
class NexLoader {
public:
    /// Parse and validate a NEX file, reading all data into memory.
    /// Returns true on success.
    bool load(const std::string& path);

    /// Access the parsed header (valid after successful load()).
    const NexHeader& header() const { return header_; }

    /// Apply the loaded NEX data to the emulator: load banks into RAM,
    /// set up screen data, configure CPU registers, border, and MMU.
    bool apply(Emulator& emu) const;

    /// Map the NEX header `ram_required` enum to its KB requirement.
    /// Returns 0 for unrecognised values (loader treats as warn+default).
    /// Spec mapping (https://wiki.specnext.dev/NEX_file_format):
    ///   0 = 768 KB, 1 = 1792 KB. Value 2 = 2048 KB is documented in
    ///   KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md G155 as in-use beyond the
    ///   canonical spec. Other values fall through to 0 (unknown).
    /// Inline so unit tests can link without pulling in jnext_core.
    static inline size_t ram_required_kb(uint8_t ram_required) {
        switch (ram_required) {
            case 0: return 768;
            case 1: return 1792;
            case 2: return 2048;
            default: return 0;
        }
    }

    /// Returns true iff the NEX header `ram_required` fits within the
    /// installed RAM byte count. Used by `apply()` to reject oversize NEX
    /// files before any bank load corrupts state. (G155)
    static inline bool ram_required_fits(uint8_t ram_required, size_t installed_bytes) {
        size_t required_kb = ram_required_kb(ram_required);
        if (required_kb == 0) return true;  // unknown → don't block load
        return required_kb <= (installed_bytes / 1024);
    }

    /// G16 fix: pre-zero bank 5 (pages 10+11 = 16 KB) before screen-data
    /// ingest. The four optional NEX screen formats (ULA / LoRes / HiRes /
    /// HiColour) all write into bank 5 starting at page 10 offset 0 with
    /// payload sizes 6912 / 12288 / 12288 / 12288 respectively. Bank 5 is
    /// 16384 bytes, so the ULA path leaves the upper 9472 bytes (and the
    /// LoRes/HiRes/HiCol paths leave the upper 4096 bytes) holding stale
    /// RAM contents from before the load. That residue is observable as
    /// attribute / pixel leak in shadow-screen / Layer 2 / certain ULA
    /// modes that consume past the ULA classic 0x1AFF boundary. See
    /// `doc/issues/BEAST-NEX-INVESTIGATION.md` § Verdict and
    /// `doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md` G16. The Layer 2
    /// screen ingest writes pages 16-21 (banks 8/9/10) — bank 5 / pages
    /// 10-11 are not in that range, so Layer 2 stays clean.
    ///
    /// Routed through the same temp-slot-7 MMU mapping that the screen
    /// ingest's write_to_ram() helper uses, so the zeroing passes through
    /// the identical machinery and exercises the same to_sram_page()
    /// shift / dual-port-bypass logic. Inline so unit tests can link
    /// without pulling in the full jnext_core (Emulator).
    static inline void zero_bank5_screen_pages(Mmu& mmu) {
        constexpr int      TEMP_SLOT  = 7;
        constexpr uint16_t SLOT_BASE  = 0xE000;
        constexpr uint16_t PAGE_SIZE  = 0x2000;  // 8 KB per MMU page
        const uint8_t saved = mmu.get_page(TEMP_SLOT);
        // Pages 10 and 11 are bank 5 (VHDL zxnext.vhd:2961-2962 mark them
        // as dual-port pages; to_sram_page() bypasses +0x20 shift for
        // these in Next mode, so logical page 10/11 → physical 10/11).
        for (uint8_t page = 10; page <= 11; ++page) {
            mmu.set_page(TEMP_SLOT, page);
            for (uint16_t off = 0; off < PAGE_SIZE; ++off) {
                mmu.write(static_cast<uint16_t>(SLOT_BASE + off), 0x00);
            }
        }
        mmu.set_page(TEMP_SLOT, saved);
    }

private:
    NexHeader header_{};
    std::vector<uint8_t> file_data_;  // all data after the 512-byte header
    bool loaded_ = false;

    // Bank loading order as specified by the NEX format
    static constexpr int kBankOrder[] = {
        5, 2, 0, 1, 3, 4, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 30, 31,
        32, 33, 34, 35, 36, 37, 38, 39,
        40, 41, 42, 43, 44, 45, 46, 47,
        48, 49, 50, 51, 52, 53, 54, 55,
        56, 57, 58, 59, 60, 61, 62, 63,
        64, 65, 66, 67, 68, 69, 70, 71,
        72, 73, 74, 75, 76, 77, 78, 79,
        80, 81, 82, 83, 84, 85, 86, 87,
        88, 89, 90, 91, 92, 93, 94, 95,
        96, 97, 98, 99, 100, 101, 102, 103,
        104, 105, 106, 107, 108, 109, 110, 111
    };
};
