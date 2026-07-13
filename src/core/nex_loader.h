#pragma once

#include "memory/mmu.h"

#include <cstddef>
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

    // -----------------------------------------------------------------
    // Loading-bar / delay fields (NEX header offsets 130-133; G156).
    // Semantics are taken directly from the reference loader,
    // tbblue/src/asm/nexload/nexload.asm (Jim Bagley / Robin
    // Verhagen-Guest, GPLv3), not from VHDL — this is a file-format
    // convention, not hardware. Local path referenced during this audit:
    // ~/src/spectrum/tbblue/src/asm/nexload/nexload.asm.
    // -----------------------------------------------------------------

    /// nexload.asm loads banks 5, 2, 0 directly (lines 520-533) before
    /// entering its main load loop, which then walks slot indices 3..111
    /// inclusive (lines 536-545, `ld a,3` .. `cp 112`) — 109 iterations,
    /// one per REMAINING bank slot, regardless of whether that bank is
    /// actually present in the file. `delay` (gated on screen presence)
    /// and `progress` (gated on loading_bar) are called on every one of
    /// those 109 iterations.
    static constexpr int kPostEarlyBankSlots = 109;

    /// Total bank "slots" the loader visits: the 3 early direct loads
    /// plus the 109-iteration loop = 112, matching kBankOrder's size
    /// (verified by static_assert below) and the range of `d` values
    /// `progress()` is called with (0..111).
    static constexpr int kTotalBankSlots = 112;

    /// nexload.asm:612-614 `delay`:
    ///   ld a,(LoadDel): or a: ret z: call rasterWait
    /// called once per post-early bank slot (kPostEarlyBankSlots total),
    /// each time waiting `loading_delay` frames — but ONLY when the NEX
    /// header's screen_flags is non-zero (nexload.asm:541 `ld
    /// a,(IsLoadingScr):or a:call nz,delay`). The delay fires for every
    /// slot, whether or not that particular bank is present in the file
    /// — this is the real (if slightly surprising) firmware behaviour,
    /// not an approximation. Pure function so it is testable without an
    /// Emulator/Mmu (BOOT-NEX-04).
    static inline uint32_t inter_bank_delay_frames(bool screen_present, uint8_t loading_delay) {
        if (!screen_present) return 0;
        return static_cast<uint32_t>(kPostEarlyBankSlots) * loading_delay;
    }

    /// nexload.asm:575-577:
    ///   ld a,(StartDel): call rasterWait
    /// a single, unconditional wait of `start_delay` frames after all
    /// banks are loaded and before jumping to the program entry point
    /// (not gated on screen presence, unlike the per-bank delay above).
    /// The sum of inter_bank_delay_frames() and start_delay is the total
    /// number of frames jnext holds the CPU idle before the loaded
    /// program starts — see Emulator::set_boot_hold_frames() (BOOT-NEX-05).
    static inline uint32_t boot_hold_frames(uint8_t start_delay, bool screen_present, uint8_t loading_delay) {
        return inter_bank_delay_frames(screen_present, loading_delay) +
               static_cast<uint32_t>(start_delay);
    }

    /// nexload.asm:616-621 `progress`:
    ///   ld a,LAYER_2_PAGE_2*2:NEXTREG_A MMU_REGISTER_6:inc a:NEXTREG_A MMU_REGISTER_7
    ///   ld a,(LoadCol):ld e,a
    ///   ld h,$fe:ld a,d:add a,a:add a,24-6:ld l,a:ld (hl),e
    ///   inc h:ld (hl),e:inc l:ld (hl),e:dec h:ld (hl),e
    /// Draws one loading-bar mark: maps physical bank 11 (LAYER_2_PAGE_2,
    /// = MMU pages 22/23) into slots 6/7 ($C000-$FFFF), then writes the
    /// `colour` byte at 4 addresses derived from the bank-slot index `d`
    /// (0..111): (0xFE00|l), (0xFF00|l), (0xFF00|l+1), (0xFE00|l+1),
    /// where l = d*2+18. All four addresses land in the same physical
    /// page (23) for every d in range. The caller is responsible for the
    /// `loading_bar != 0` gate (nexload.asm's own `progress` does this
    /// internally via `ld a,(LoadBar):or a:ret z`; here NexLoader::apply()
    /// checks header_.loading_bar before calling, per bank slot, so the
    /// call itself is unconditional and independently testable — BOOT-NEX-03/06).
    /// Inline so unit tests can link without pulling in jnext_core
    /// (mirrors ram_required_kb / zero_bank5_screen_pages above).
    static inline void render_progress_mark(Mmu& mmu, uint8_t d, uint8_t colour) {
        constexpr uint8_t PAGE_LO = 22;   // LAYER_2_PAGE_2 (bank 11) low 8K page
        constexpr uint8_t PAGE_HI = 23;   // LAYER_2_PAGE_2 (bank 11) high 8K page
        constexpr int SLOT_LO = 6;
        constexpr int SLOT_HI = 7;

        const uint8_t saved_lo = mmu.get_page(SLOT_LO);
        const uint8_t saved_hi = mmu.get_page(SLOT_HI);
        mmu.set_page(SLOT_LO, PAGE_LO);
        mmu.set_page(SLOT_HI, PAGE_HI);

        // nexload.asm:620: ld h,$fe:ld a,d:add a,a:add a,24-6:ld l,a
        const uint8_t l = static_cast<uint8_t>(d * 2 + 18);  // 24-6 = 18
        const uint16_t addr_fe_l  = static_cast<uint16_t>(0xFE00 | l);
        const uint16_t addr_ff_l  = static_cast<uint16_t>(0xFF00 | l);
        const uint8_t  l1         = static_cast<uint8_t>(l + 1);
        const uint16_t addr_ff_l1 = static_cast<uint16_t>(0xFF00 | l1);
        const uint16_t addr_fe_l1 = static_cast<uint16_t>(0xFE00 | l1);

        // nexload.asm:620: ld (hl),e:inc h:ld (hl),e:inc l:ld (hl),e:dec h:ld (hl),e
        mmu.write(addr_fe_l,  colour);
        mmu.write(addr_ff_l,  colour);
        mmu.write(addr_ff_l1, colour);
        mmu.write(addr_fe_l1, colour);

        mmu.set_page(SLOT_LO, saved_lo);
        mmu.set_page(SLOT_HI, saved_hi);
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
        // Pages 10 and 11 are bank 5 — the dedicated 16K dual-port VRAM
        // (VHDL bank5_ram dpram2, zxnext.vhd:6558; Task 25). Writing via
        // the MMU slot routes rebuild_ptr()'s bank-5 gate to the
        // Mmu::bank5_vram_ buffer in Next mode (flat pages 10/11 on
        // standalone machines).
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
    static_assert(sizeof(kBankOrder) / sizeof(kBankOrder[0]) == kTotalBankSlots,
                  "kBankOrder must have exactly kTotalBankSlots (112) entries — "
                  "loading-bar 'd' positions are kBankOrder indices (G156)");
};
