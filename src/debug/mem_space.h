#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::MemSpace -- WHICH memory a byte lives in.
//
// std headers only, and that is the point of this file being separate from
// `debug/inspect.h`: `memory/mmu.h` has to RETURN this type (it is the result
// of the one read arbiter, `Mmu::route()`), and `inspect.h` is a published
// header that `test/lint-debug-headers.sh` keeps clear of `memory/mmu.h`.
// Putting the type here lets the core header include it without pulling the
// inspection API into the hot path, and lets `inspect.h` re-export it.
//
// TWO ROLES, ONE TYPE (GH #339, epic #23 OD-13): a debugger client names a
// space to `peek` / `poke` (`Cpu`, `Page`, `Rom`), and the read arbiter names
// the space that SUPPLIED a byte (every kind below except `Cpu`). `FetchSource`
// is the second role's name for greppability; it is not a second type.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace jnext {
namespace dbg {

/// WHICH memory a `peek` / `poke` addresses, or a CPU read came from, index
/// included (§4.2 INS-02; GH #339).
///
/// The index is PART OF THE SPACE, not something a caller composes from
/// `SlotInfo`'s other fields. Round 4 of the design review caught both the Qt
/// and the DZRP frontend about to build a space out of `effective_page +
/// is_rom`, which is wrong on the Next: a ROM slot's `effective_page` is an
/// un-shifted `ram_` page index (0..7), outside `Page{}`'s NR 0x50-0x57 number
/// space entirely. So `SlotInfo::space` hands the complete answer over
/// (REQ-qt-31).
///
/// EQUALITY IS BY DECODE PATH, not by physical store. `Layer2{p}` and `Page{p}`
/// share external SRAM for every p outside {0x0A, 0x0B, 0x0E}, and
/// `DivMmcRam{b}`, `Config{b}`, the Multiface and alt-ROM windows all alias
/// SRAM pages some `Page{}` also reaches; they stay DISTINCT here because the
/// VHDL arbiter reaches them by different routes and a debugger that shows
/// which one answered must not collapse them.
///
/// Every value the arbiter produces fits a 12-bit packing: `kind <= 15` and
/// `index <= 0xFF` (pinned by mmu_test FSRC-09, and by the static_asserts in
/// `debug_types_check.cpp`). `Cpu` is never an arbiter result.
struct MemSpace {
    enum class Kind : uint8_t {
        /// The live CPU mapping, overlays included (DivMMC, Multiface,
        /// Layer 2). `index` unused. Reads go through `Mmu::peek()` -- NO
        /// floating-bus latch update (F1). Never the result of the read
        /// arbiter: it is the peek side's "whatever the CPU would see".
        Cpu = 0,
        /// A physical 8 K page, `index` = the NR 0x50-0x57 page number
        /// (0..223). The backend does the VHDL routing (`to_sram_page`, page
        /// 0x0E -> the bank-7 BRAM buffer); 0xFE / 0xFF are the ROM sentinels
        /// and refuse with `Result::InvalidPage`. Addresses the physical page
        /// REGARDLESS of any overlay mapped over a slot -- that is the point of
        /// the slot view (design-qt QMP-06).
        ///
        /// As an arbiter result: the MMU page `n` (< 0xE0) of the slot
        /// (zxnext.vhd:3037-3043 for slots 0/1, :3059-3065 for slots 2-7; `n` is
        /// `mem_active_page`, :2952). On the Next that is SRAM page `n + 0x20`
        /// (:2964), except n = 0x0A / 0x0B (the bank-5 BRAM) and n = 0x0E (the
        /// bank-7 BRAM) (:2961-2962, :6562, :6670).
        Page,
        /// A 16 KB ROM image, `index` 0..3, addresses 0..0x3FFF within it.
        /// Read-only: `poke` refuses with `Result::RefusedReadOnly`. On a
        /// `rom_in_sram_` machine (Next mode) it is SRAM pages `2*index` and
        /// `2*index+1`; on 48K/128K/+3 it is the `Rom` object's image (48K has
        /// 1, 128K 2, +3 4). The NR 0x8C alt-ROM LOCK bits (5:4) are folded
        /// into the ROM select, so `Rom{0..3}` covers every image legacy
        /// paging can select. The NR 0x8C bit-7 ALTERNATE ROM itself is NOT
        /// among them: with bit 7 set and bit 6 clear the CPU reads slots 0/1
        /// from SRAM pages 12-15 (the alt-ROM read override in `Mmu::read`),
        /// an overlay like DivMMC's -- `Cpu` sees it, no `Rom{}` does, but the
        /// arbiter names it `AltRom`. (This comment said the overrides made
        /// `Rom{0..3}` complete; that held for the lock bits only -- corrected,
        /// comment only, GH #12.)
        ///
        /// As an arbiter result: `index` = `sram_rom`, the half is cpu_a(13)
        /// (zxnext.vhd:3052-3057).
        Rom,

        // ---- identity-only kinds (GH #339): the arbiter names them, `peek` and
        // ---- `poke` refuse them with `Result::Unsupported` until a consumer
        // ---- (E23-21 for DivMmcRam) adds the access with its own rows.

        /// The 8 KB boot ROM (`bootrom_en`), mirrored through 0x0000-0x3FFF.
        /// `cpu_di <= bootrom_do` sits ABOVE the whole SRAM arbiter
        /// (zxnext.vhd:1856-1857; the BRAM is addressed by `cpu_a(12:0)`,
        /// :3199-3204). `index` 0.
        BootRom,
        /// The Multiface ROM half (0x0000-0x1FFF while `mf_mem_en`):
        /// SRAM page 0x0A on the Next (zxnext.vhd:2923, :3030-3035).
        /// `index` 0.
        MultifaceRom,
        /// The Multiface RAM half (0x2000-0x3FFF while `mf_mem_en`):
        /// SRAM page 0x0B (:3030-3035). `index` 0.
        MultifaceRam,
        /// The DivMMC ROM (8 KB): SRAM page 0x08 (zxnext.vhd:2921, :3084-3090;
        /// divmmc.vhd:94,98). `index` 0.
        DivMmcRom,
        /// A DivMMC RAM bank: `index` = bank 0..15 (the bank at page 0 is 3
        /// under MAPRAM), SRAM page 0x10 + bank (zxnext.vhd:2926, :3092-3098;
        /// divmmc.vhd:95-96,99). Distinct from `Page{b}` (GH #339 D4).
        DivMmcRam,
        /// The alternate ROM read override: `index` = `sram_alt_128_n`
        /// (0 = Alt ROM0 128k, 1 = Alt ROM1 48k), SRAM pages 0x0C + 2*index +
        /// cpu_a(13) (zxnext.vhd:2924-2925, :3116-3122, selector :2981-3008).
        AltRom,
        /// The Layer 2 CPU read mapping: `index` = `layer2_active_page`
        /// (8 bits, zxnext.vhd:2966-2969), SRAM page `index + 0x20` (:2971),
        /// never the BRAMs (:3100-3107).
        Layer2,
        /// The config-mode window onto SRAM: `index` = NR 0x04 (a 16 KB bank),
        /// SRAM pages 2*index + cpu_a(13) (zxnext.vhd:3044-3050).
        Config,
        /// No memory answers: the read returns the floating bus (jnext: 0xFF).
        /// Slots 2-7 holding an MMU page >= 0xE0 (zxnext.vhd:3060-3061) and a
        /// Layer 2 page >= 0xE0 (:3101-3102). `index` 0.
        Floating,
    };

    Kind kind = Kind::Cpu;

    /// ONE WIDTH FOR EVERY PAGE NUMBER IN THIS API: `uint16_t`, the same as
    /// `Event::phys_page` and `EventFilter::page` / `EventFilter::pages`. A page
    /// number crosses between them constantly (a `Mem` delivery's `phys_page`
    /// handed straight to `MemSpace::page()` to peek what was written), and a
    /// narrower type here would put a silent narrowing conversion on every one
    /// of those hops. Valid NR 0x50-0x57 numbers are 0..223 and the ROM
    /// sentinels are 0xFE / 0xFF, so the value range needs only 8 bits; the
    /// width is for the seam, not the range.
    uint16_t index = 0;

    static constexpr MemSpace cpu();
    static constexpr MemSpace page(uint16_t p);
    static constexpr MemSpace rom(uint16_t i);
    static constexpr MemSpace boot_rom();
    static constexpr MemSpace multiface_rom();
    static constexpr MemSpace multiface_ram();
    static constexpr MemSpace divmmc_rom();
    static constexpr MemSpace divmmc_ram(uint16_t bank);
    static constexpr MemSpace alt_rom(uint16_t i);
    static constexpr MemSpace layer2(uint16_t p);
    static constexpr MemSpace config(uint16_t bank);
    static constexpr MemSpace floating();
};

constexpr MemSpace MemSpace::cpu() { return MemSpace{Kind::Cpu, 0}; }
constexpr MemSpace MemSpace::page(uint16_t p) { return MemSpace{Kind::Page, p}; }
constexpr MemSpace MemSpace::rom(uint16_t i) { return MemSpace{Kind::Rom, i}; }
constexpr MemSpace MemSpace::boot_rom() { return MemSpace{Kind::BootRom, 0}; }
constexpr MemSpace MemSpace::multiface_rom() { return MemSpace{Kind::MultifaceRom, 0}; }
constexpr MemSpace MemSpace::multiface_ram() { return MemSpace{Kind::MultifaceRam, 0}; }
constexpr MemSpace MemSpace::divmmc_rom() { return MemSpace{Kind::DivMmcRom, 0}; }
constexpr MemSpace MemSpace::divmmc_ram(uint16_t bank) { return MemSpace{Kind::DivMmcRam, bank}; }
constexpr MemSpace MemSpace::alt_rom(uint16_t i) { return MemSpace{Kind::AltRom, i}; }
constexpr MemSpace MemSpace::layer2(uint16_t p) { return MemSpace{Kind::Layer2, p}; }
constexpr MemSpace MemSpace::config(uint16_t bank) { return MemSpace{Kind::Config, bank}; }
constexpr MemSpace MemSpace::floating() { return MemSpace{Kind::Floating, 0}; }

constexpr bool operator==(MemSpace a, MemSpace b) {
    return a.kind == b.kind && (a.kind == MemSpace::Kind::Cpu || a.index == b.index);
}
constexpr bool operator!=(MemSpace a, MemSpace b) { return !(a == b); }

/// The same type in the role the read arbiter uses it: what SUPPLIES an opcode
/// fetch or a read (GH #339). Five consumer sub-issues of epic #23 name this
/// role; the alias makes a grep find them. It is not a second type.
using FetchSource = MemSpace;

}  // namespace dbg
}  // namespace jnext
