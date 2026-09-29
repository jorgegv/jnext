#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg — the inspection value types.
//
// Realises §4.2 (CAP-INS), §4.2a (the write half), §4.4 (CAP-TIME's `Time` /
// `MachineInfo`), CTL-13's `RunState`, CTL-11's `CorruptionIncident`, §4.5's
// `MatrixKey` / capture enums and §4.6's `RewindRange` of
// doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md, for work package B0 (§10.1).
//
// WHAT IS REUSED, NOT REDEFINED. `Z80Registers` (`cpu/z80_cpu.h`),
// `RasterState` (`debug/raster_state.h`), `TraceEntry` (`debug/trace.h`),
// `CallFrame` (`debug/call_stack.h`), `SpriteEngine::SpriteInfo`
// (`video/sprites.h`), `DisasmLine` / `DisasmReadFn` (`debug/disasm.h`) and
// `MachineType` (`memory/contention.h`) are the project's own published value
// types and are used as they are. Each of those headers is self-contained (std
// includes only) — none of them drags `Emulator`, `Mmu`, SDL or Qt in. THAT is
// the include budget of this file, and it is deliberate: a frontend that
// includes `debug/debugger.h` must NOT end up with `core/emulator.h`, which is
// the whole point of epic #276.
//
// WHAT IS BACKEND-OWNED AND WHY — FIVE types, each saying so at its definition:
// `SlotInfo` (INS-03 — no such struct exists in the tree; `mmu.h:74-78` is the
// accessor PAIR that feeds it), `PaletteId` (INS-15 needs the eight hardware
// banks PLUS `UlaActive`, which `::PaletteId` cannot express), `Layer` (INS-14's
// eight views live in a `Q_OBJECT` header today), `ClipLayer` (INS-15's four
// NR 0x18-0x1C windows, a different set from those eight views) and `StepMode`
// (CTL-13 must not publish the internal `debug_state.h`).
//
// THEY ARE NOT ALL PINNED THE SAME WAY, and an earlier version of this banner
// said `debug_types_check.cpp` "pins each of them against the thing it mirrors",
// which is false for three of the five (`Layer`, `ClipLayer` and `SlotInfo`):
//
//   * `StepMode`  — pinned VALUE BY VALUE against `::StepMode`.
//   * `PaletteId` — values 0..7 pinned value by value against `::PaletteId`;
//                   `UlaActive` pinned at 8, past the hardware banks.
//   * `Layer`     — only its COUNT is pinned. Its eight views have no non-Qt
//                   counterpart to be diffed against, so the count via
//                   `Layer::Count` is the whole available signal.
//   * `ClipLayer` — likewise count-only (`CLIP_LAYER_COUNT == 4`); the four
//                   hardware clip windows are not an enum anywhere else.
//   * `SlotInfo`  — mirrors NOTHING. There is no `SlotInfo` in the tree to
//                   diff against; what the check TU pins is its trivial
//                   copyability, and its fields are checked by reading the
//                   accessors named at each one.
// ---------------------------------------------------------------------------

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "audio/audio_mute.h"    // AudioMute::* mute-mask bits  (INS-10)
#include "cpu/z80_cpu.h"         // Z80Registers; also forward-declares MachineType
#include "debug/call_stack.h"    // CallFrame, CallType  (INS-12)
#include "debug/disasm.h"        // DisasmLine, DisasmReadFn  (INS-11)
#include "debug/events.h"        // ClientId, EventId, Access  (CTL-13's PauseReason)
#include "debug/raster_state.h"  // RasterState  (INS-06)
#include "debug/result.h"
#include "debug/trace.h"         // TraceEntry  (INS-13)
#include "memory/contention.h"   // MachineType enumerators  (INS-19)
#include "video/sprites.h"       // SpriteEngine::SpriteInfo  (INS-08)

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// The two {ptr, size} views. C++17 has no std::span (§4 preamble), and a
// 16 KB pattern RAM or a 320x256 framebuffer must not be copied to be read.
//
// A view is valid while the machine stays paused and nothing is remapped —
// i.e. for the duration of the inspection that produced it. Copy the bytes out
// if you need them to outlive that.
// ---------------------------------------------------------------------------

// INS-08 / INS-09 / INS-14 use these three.
struct ConstBytes {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    bool empty() const { return size == 0; }
};

struct ConstU16s {
    const uint16_t* data = nullptr;
    size_t          size = 0;
    bool empty() const { return size == 0; }
};

struct ConstU32s {
    const uint32_t* data = nullptr;
    size_t          size = 0;
    bool empty() const { return size == 0; }
};

// ---------------------------------------------------------------------------
// INS-02 — memory spaces
// ---------------------------------------------------------------------------

/// WHICH memory a `peek` / `poke` addresses, index included (§4.2 INS-02).
///
/// The index is PART OF THE SPACE, not something a caller composes from
/// `SlotInfo`'s other fields. Round 4 of the design review caught both the Qt
/// and the DZRP frontend about to build a space out of `effective_page +
/// is_rom`, which is wrong on the Next: a ROM slot's `effective_page` is an
/// un-shifted `ram_` page index (0..7), outside `Page{}`'s NR 0x50-0x57 number
/// space entirely. So `SlotInfo::space` hands the complete answer over
/// (REQ-qt-31).
struct MemSpace {
    enum class Kind : uint8_t {
        /// The live CPU mapping, overlays included (DivMMC, Multiface,
        /// Layer 2). `index` unused. Reads go through `Mmu::peek()` — NO
        /// floating-bus latch update (F1).
        Cpu = 0,
        /// A physical 8 K page, `index` = the NR 0x50-0x57 page number
        /// (0..223). The backend does the VHDL routing (`to_sram_page`, page
        /// 0x0E → the bank-7 BRAM buffer); 0xFE / 0xFF are the ROM sentinels
        /// and refuse with `Result::InvalidPage`. Addresses the physical page
        /// REGARDLESS of any overlay mapped over a slot — that is the point of
        /// the slot view (design-qt QMP-06).
        Page,
        /// A 16 KB ROM image, `index` 0..3, addresses 0..0x3FFF within it.
        /// Read-only: `poke` refuses with `Result::RefusedReadOnly`. On a
        /// `rom_in_sram_` machine (Next mode) it is SRAM pages `2·index` and
        /// `2·index+1`; on 48K/128K/+3 it is the `Rom` object's image (48K has
        /// 1, 128K 2, +3 4). The NR 0x8C alt-ROM LOCK bits (5:4) are folded
        /// into the ROM select, so `Rom{0..3}` covers every image legacy
        /// paging can select. The NR 0x8C bit-7 ALTERNATE ROM itself is NOT
        /// among them: with bit 7 set and bit 6 clear the CPU reads slots 0/1
        /// from SRAM pages 12-15 (the alt-ROM read override in `Mmu::read`),
        /// an overlay like DivMMC's — `Cpu` sees it, no `Rom{}` does. (This
        /// comment said the overrides made `Rom{0..3}` complete; that held for
        /// the lock bits only — corrected, comment only, GH #12.)
        Rom,
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
};

constexpr MemSpace MemSpace::cpu() { return MemSpace{Kind::Cpu, 0}; }
constexpr MemSpace MemSpace::page(uint16_t p) { return MemSpace{Kind::Page, p}; }
constexpr MemSpace MemSpace::rom(uint16_t i) { return MemSpace{Kind::Rom, i}; }

constexpr bool operator==(MemSpace a, MemSpace b) {
    return a.kind == b.kind && (a.kind == MemSpace::Kind::Cpu || a.index == b.index);
}
constexpr bool operator!=(MemSpace a, MemSpace b) { return !(a == b); }

/// INS-02 — the two page numbers NR 0x50-0x57 uses as the VHDL ROM sentinel. A
/// `MemSpace::page()` naming either refuses with `Result::InvalidPage`.
/// `uint16_t`, matching `MemSpace::index` rather than the 8-bit value they hold:
/// these are compared against an index, and integer promotion would make a
/// narrower type work while reading as a different kind of number.
constexpr uint16_t PAGE_SENTINEL_ROM_LO = 0xFE;
constexpr uint16_t PAGE_SENTINEL_ROM_HI = 0xFF;

// ---------------------------------------------------------------------------
// INS-03 — MMU slots
// ---------------------------------------------------------------------------

/// One of the eight 8 K CPU slots (INS-03).
///
/// BACKEND-OWNED, and there is nothing to extend: the tree has no `SlotInfo`
/// struct. `mmu.h:74-78` — which §4 INS-03 cites — is
/// `Mmu::get_effective_page()` + `Mmu::is_slot_rom()`, the accessor pair the
/// backend reads to fill this in.
struct SlotInfo {
    /// The NR 0x50-0x57 value as written, including the 0xFF ROM sentinel.
    uint8_t nr_page = 0;

    /// The page actually backing the slot — `Mmu::get_effective_page(slot)`,
    /// which resolves the sentinel through legacy paging. For a ROM slot this
    /// is the un-shifted `ram_` page index (0..7 on the Next); it is NOT an NR
    /// page number and must never be handed to `MemSpace::page()`.
    uint8_t effective_page = 0;

    /// `Mmu::is_slot_rom(slot)` — the slot is read-only.
    bool is_rom = false;

    /// The backing store of this slot's 8 KB, complete and ready to peek:
    /// `MemSpace::page(nr_page)` for a RAM slot, `MemSpace::rom(effective_page
    /// >> 1)` for a ROM slot.
    MemSpace space;

    /// Byte offset of this slot's 8 KB within `space`: 0 for a RAM page,
    /// `(effective_page & 1) * 0x2000` for a ROM slot (a 16 KB image holds two
    /// 8 K slots).
    uint16_t space_offset = 0;
};

/// INS-03 — the legacy paging ports, as last written (`paging_ports()`).
struct PagingPorts {
    uint8_t port_7ffd = 0;  ///< 128K bank select
    uint8_t port_1ffd = 0;  ///< +3 extended paging
    uint8_t port_dffd = 0;  ///< Next extended high-bank bits
};

// ---------------------------------------------------------------------------
// INS-01 — registers
// ---------------------------------------------------------------------------

/// Which register `set_register()` writes (INS-01, §4.2a).
///
/// The 12 pairs, EVERY 8-bit half including `F`, plus I, R, IFF1, IFF2, IM.
/// There is deliberately no "set all": `Z80Cpu::set_registers()` — the only
/// setter today — assigns the whole struct, which is how a frontend clobbers
/// `I` and `R` while writing `A` (a real GDB `G`-packet hazard).
///
/// `PC` carries an obligation on the implementation, not a property of the
/// code: setting it must CLEAR `Z80Registers::halted`. Nothing does that today.
enum class RegId : uint8_t {
    // 16-bit pairs
    AF = 0, BC, DE, HL,
    AF2, BC2, DE2, HL2,
    IX, IY, SP, PC,
    // 8-bit halves
    A, F, B, C, D, E, H, L,
    A2, F2, B2, C2, D2, E2, H2, L2,
    IXH, IXL, IYH, IYL,
    // the rest
    I, R, IFF1, IFF2, IM,

    /// NOT A REGISTER — the count, and it must stay last. See
    /// `EventKind::Count` for why a trailing sentinel rather than an assert on
    /// the last real enumerator.
    Count,
};

/// INS-01 — number of `RegId` enumerators — 12 pairs + 20 halves + 5 = 37.
/// Derived from the trailing sentinel, so an APPENDED register fires the
/// dependent `static_assert`.
constexpr size_t REG_ID_COUNT = static_cast<size_t>(RegId::Count);

// ---------------------------------------------------------------------------
// CTL-13 — run state
// ---------------------------------------------------------------------------

/// CTL-13 — the step mode `state()` reports.
///
/// A BACKEND-OWNED MIRROR of the internal `::StepMode` (`debug/debug_state.h`),
/// which §3.1 keeps unpublished. `debug_types_check.cpp` asserts the two agree
/// value by value, so the mirror cannot drift.
enum class StepMode : uint8_t {
    None = 0,
    Into,
    Over,
    Out,
    RunToCycle,
    StepBack,
    RunBackToCycle,
};

/// CTL-13 — the closed set of pause reasons.
struct PauseReason {
    enum class Kind : uint8_t {
        /// Not paused.
        None = 0,
        /// A client's `pause()`. `by` names it.
        User,
        /// An `Execute` subscription stopped. `id` names it.
        Breakpoint,
        /// A `Mem` or `Port` subscription stopped. `id`, `access`, `addr`.
        Watch,
        /// A step verb completed.
        Step,
        /// A `run_to` / `run_to_cycle` / end-of-frame / end-of-scanline target
        /// was reached. `id` names the transient subscription where there was
        /// one.
        RunTo,
        /// The magic breakpoint opcode (CTL-14). **UNOWNED**: see below.
        Magic,
        /// A failed rewind or state load left the machine corrupt (CTL-11).
        /// **UNOWNED**: see below.
        Corrupt,
        /// A script's explicit stop. `id` names its subscription, `text` its
        /// message — which is how a regression row tells a script verdict from
        /// a failed manifest check.
        Script,
    };

    /// TWO REASONS HAVE NO OWNING CLIENT (`by == CLIENT_NONE`): `Magic` and
    /// `Corrupt`. Neither is anyone's verb — the magic opcode is the guest's and
    /// the corruption is the machine's — so SES-01's "detach resumes a machine
    /// paused BY THIS CLIENT" must not resume either of them, however many
    /// clients come and go. A client id of `CLIENT_NONE` is what says so, and
    /// implementing that rule is B's and B5's (owner decision, Revision 6);
    /// stating it is B0's.
    Kind        kind   = Kind::None;
    ClientId    by     = CLIENT_NONE;
    EventId     id     = EVENT_NONE;
    Access      access = Access::None;
    uint16_t    addr   = 0;
    std::string text;
};

/// CTL-13 — what the machine is doing. Cheap to take, and the one thing every
/// frontend polls.
struct RunState {
    bool        paused    = false;
    StepMode    step_mode = StepMode::None;
    PauseReason pause_reason;
    uint64_t    cycle = 0;
    uint32_t    frame = 0;
    uint16_t    pc    = 0;
};

/// CTL-11 — a latched corruption incident. The `ResumeGuard` policy
/// (`debug/resume_guard.h`) decides from the pair; the modal stays in Qt and an
/// unacknowledged remote gets `Result::RefusedCorrupt`.
struct CorruptionIncident {
    /// Which subsystem's `load_state` failed — `Emulator::last_state_error()`.
    std::string subsystem;
    /// `Emulator::state_error_generation()`. Monotonic, and what
    /// `acknowledge_corruption()` takes: acking generation N does not ack a
    /// later, fresh incident.
    uint64_t generation = 0;
};

// ---------------------------------------------------------------------------
// INS-07 / INS-19 — time and machine
// ---------------------------------------------------------------------------

/// INS-07 / TIME-01 — where the machine is in time. Deterministic only — no wall clock
/// appears anywhere in this API (§4.4).
struct Time {
    /// The 28 MHz master cycle counter.
    uint64_t master_cycle = 0;
    /// `Emulator::monotonic_tstates()`.
    uint64_t tstates_total = 0;
    /// The CURRENT frame's tag — the pre-increment number (F2). The raw
    /// counter reads K+1 during frame K, so this is `frame_num_ - 1`: the same
    /// number a rewind slot carries, and the same one `--delayed-keypress-
    /// frames N` lands on. Reading the raw counter instead makes `on frame N`
    /// fire one frame early.
    uint32_t frame = 0;
    /// Master cycles elapsed since this frame began.
    uint64_t cycle_in_frame = 0;
    /// VHDL `vc` — the RAW frame line counter. `raster()` (INS-06) is what
    /// converts the four counter domains; do not do it here.
    int16_t vc_raw = 0;
    /// VHDL `hc` — the RAW frame pixel counter.
    int16_t hc_raw = 0;
};

/// INS-19 / TIME-01 — the per-machine constants. Every value comes from the LIVE
/// `VideoTiming` and `ContentionModel` the emulator runs on — there is
/// deliberately no second table of raster constants in the debugger.
struct MachineInfo {
    MachineType type = MachineType::ZXN_ISSUE2;
    /// 28 MHz / this = the CPU clock. 8, 4, 2 or 1 (NR 0x07).
    int cpu_divisor = 8;
    // TWO CLOCK DOMAINS, NEVER BOTH CALLED "CYCLES". The first cut of this
    // struct had `cycles_per_line` / `cycles_per_frame` documented as T-states
    // while `Time::master_cycle` and `run_to_cycle()` are 28 MHz master cycles
    // — 4x apart under one word, which is the readiest way to be off by four.
    // Both domains are carried, under `MachineTiming`'s own field names
    // (`src/core/emulator_config.h`), which is where the backend reads them
    // from; `debug_types_check.cpp` pins the names and the types against that
    // struct, so a rename on either side is a build failure.

    /// CPU T-states at the 3.5 MHz reference. Same domain as
    /// `Time::tstates_total`.
    int tstates_per_line  = 0;
    int tstates_per_frame = 0;

    /// 28 MHz master cycles. Same domain as `Time::master_cycle`,
    /// `Time::cycle_in_frame`, `run_to_cycle()` and `EventFilter::cycle`.
    uint64_t master_cycles_per_line  = 0;
    uint64_t master_cycles_per_frame = 0;

    /// Lines per frame, i.e. `vc_max + 1` — `MachineTiming::lines_per_frame`.
    int lines = 0;
    /// Frames per second, derived — not a nominal 50.
    double fps = 0.0;
    /// VHDL `c_max_hc` / `c_max_vc`.
    int hc_max = 0;
    int vc_max = 0;
    /// VHDL `c_max_hblank` / `c_max_vblank`.
    int max_hblank = 0;
    int max_vblank = 0;
    /// `VideoTiming::display_origin()` — raw `hc` / `vc` of the top-left of the
    /// active display. Two ints rather than the `RasterPos` of `video/timing.h`,
    /// so this header does not pull the timing model in for one pair.
    int display_origin_hc = 0;
    int display_origin_vc = 0;
    /// `VideoTiming::vblank_top()` — `framebuffer_row = vc - vblank_top`.
    int vblank_top = 0;
};

// ---------------------------------------------------------------------------
// INS-08 — sprites
// ---------------------------------------------------------------------------

/// The decoded sprite record, reused from the sprite engine (`video/sprites.h`
/// — "Decoded sprite info for debugger display", which is exactly this).
/// Defining a parallel struct would put a second copy of the 5-byte attribute
/// decode in the tree, free to drift from the VHDL-verified one.
using SpriteInfo = SpriteEngine::SpriteInfo;

/// INS-08 — bytes in one sprite's raw attribute set (`sprite_attr_raw`).
constexpr size_t SPRITE_ATTR_BYTES = 5;

/// INS-08 — sprites the engine has.
constexpr size_t SPRITE_COUNT = 128;

/// Bytes of sprite pattern RAM (INS-08 `pattern_ram()`: "16 KB").
constexpr size_t PATTERN_RAM_BYTES = 16384;

/// INS-08 / INS-15 — a clip window, as NR 0x18-0x1C programs it (`sprite_clip()`, INS-15
/// `clip_window()`). Read from the LIVE layer state, never from the rotating
/// NR 0x18-0x1C write shadows.
struct ClipWindow {
    uint8_t x1 = 0x00;
    uint8_t x2 = 0xFF;
    uint8_t y1 = 0x00;
    uint8_t y2 = 0xBF;
};

// ---------------------------------------------------------------------------
// INS-09 — copper
// ---------------------------------------------------------------------------

/// INS-09 — the Copper's live state.
struct CopperState {
    /// Instruction index 0..1023.
    uint16_t pc = 0;
    /// `Copper::is_running()`, i.e. `mode != 0`.
    bool running = false;
    /// `Copper::mode()` — NR 0x62 bits 7:6, the stop / start / reset-at-vblank
    /// mode.
    uint8_t mode = 0;
    /// The 1024-entry instruction RAM, as 16-bit words. A VIEW into the live
    /// Copper's RAM (see `ConstU16s`), not a 2 KB copy.
    ConstU16s program;
};

// ---------------------------------------------------------------------------
// INS-10 — audio
// ---------------------------------------------------------------------------

/// INS-10 — registers per AY/YM chip.
constexpr size_t AY_REGISTER_COUNT = 16;

/// INS-10 — chips in the TurboSound stack.
constexpr size_t AY_CHIP_COUNT = 3;

/// Which volume/envelope curve the three chips use — `TurboSound::ay_mode()`.
/// Named rather than a bare bool because a frontend that gets the polarity
/// backwards mislabels every chip and nothing notices.
///
/// NOT THE WHOLE REGISTER, and a panel author must not read it as such: the
/// hardware field is NR 0x06 **bits 1:0**, four values, and one of them holds
/// all three AY chips in reset — which jnext models
/// (`src/audio/turbosound.h`). INS-10 asks only for `ay_mode()`, whose backing
/// accessor is a bool, so this enum inherits that two-value view faithfully and
/// cannot report the reset state. A frontend that needs it reads NR 0x06
/// through `nextreg_peek()`.
enum class AyChipMode : uint8_t { Ym = 0, Ay = 1 };

/// NR 0x08 bit 5 — channel-to-side assignment (`TurboSound::stereo_mode()`).
enum class StereoMode : uint8_t { Abc = 0, Acb = 1 };

// INS-10 — the `audio_mute_mask` bits are `AudioMute::{AY0, AY1, AY2, DAC,
// BEEPER, AY_ALL, ALL, NONE}` (`audio/audio_mute.h`), REUSED rather than
// redefined: that header already documents why the mask has no hardware
// analogue, why it must stay invisible to the Z80, and why it is excluded from
// save_state() and reset(). A second copy of the bit assignment here is exactly
// the drift the reuse rule exists to prevent.

// ---------------------------------------------------------------------------
// INS-14 / INS-15 — layers, palettes
// ---------------------------------------------------------------------------

/// INS-14 — which layer view `render_layer()` draws.
///
/// BACKEND-OWNED: the eight views exist today only as
/// `VideoLayerView::Layer`, nested in a `Q_OBJECT` class
/// (`src/debugger/video_panel.h`), and INS-14 requires the renderer to become a
/// Qt-free function. `debug_types_check.cpp` pins the enumerator count.
enum class Layer : uint8_t {
    /// Every layer composited exactly as the emulator window shows it.
    Composite = 0,
    /// ULA standard screen — bank 5.
    UlaPrimary,
    /// ULA 128K shadow screen — bank 7, port 0x7FFD bit 3.
    UlaShadow,
    Layer2Active,
    Layer2Shadow,
    /// Sprite layer over a transparent background.
    Sprites,
    /// Tilemap layer over a transparent background.
    Tilemap,
    /// The NR 0x4A fallback colour, per scanline. Belongs to no layer.
    Background,

    /// NOT A VIEW — the count, and it must stay last. See `EventKind::Count`.
    Count,
};

/// Derived from the trailing sentinel, so an APPENDED view fires the dependent
/// `static_assert` — which is the only signal there is, because the eight views
/// have no non-Qt counterpart to be diffed against.
constexpr size_t LAYER_COUNT = static_cast<size_t>(Layer::Count);

/// Width in pixels of every `render_layer()` destination and of `framebuffer()`
/// (INS-14: "width 640"). Rows 0..vc are drawn over a 0x00000000 fill, where
/// alpha 0 means transparent.
///
/// Its oracle is `Renderer::FB_WIDTH` — the framebuffer these pixels actually
/// go into — not `SpriteEngine::DISPLAY_WIDTH`, which is the same number for an
/// unrelated reason (the sprite engine's own full pixel width) and would go on
/// agreeing if the framebuffer changed. Both are asserted in
/// `debug_types_check.cpp`, the renderer's as the one that matters.
constexpr size_t RENDER_WIDTH = 640;

/// INS-15 — which of the four clip windows `clip_window()` returns.
///
/// NOT `Layer` above: these are the four hardware clip registers of
/// NR 0x18-0x1C, a different set from the eight render views. §4 spelled BOTH
/// `Layer` when these headers were written, which is not expressible as one
/// type; Revision 6 adopted the split, so INS-14 is `Layer` and INS-15 is
/// `ClipLayer`.
/// `Count` is not a window; it must stay last. See `EventKind::Count`.
enum class ClipLayer : uint8_t { Layer2 = 0, Sprites, Ula, Tilemap, Count };

/// The four hardware clip windows of NR 0x18-0x1C, from the trailing sentinel.
constexpr size_t CLIP_LAYER_COUNT = static_cast<size_t>(ClipLayer::Count);

/// INS-15 — which palette bank to read or write.
///
/// BACKEND-OWNED SUPERSET of `::PaletteId` (`video/palette.h`): values 0..7 are
/// that enum's eight hardware banks, value by value (`debug_types_check.cpp`
/// asserts it), and `UlaActive` is the extra INS-15 asks for — "whichever ULA
/// bank NR 0x43 has selected right now", which the hardware enum cannot name.
enum class PaletteId : uint8_t {
    UlaFirst      = 0,
    Layer2First   = 1,
    SpriteFirst   = 2,
    TilemapFirst  = 3,
    UlaSecond     = 4,
    Layer2Second  = 5,
    SpriteSecond  = 6,
    TilemapSecond = 7,
    /// The ULA bank currently selected. Resolves to `UlaFirst` or `UlaSecond`.
    UlaActive     = 8,
};

/// INS-15 — the ULA's screen-selection state (`ula_screen_regs()`).
struct UlaScreenRegs {
    /// 5 or 7 — which RAM bank the ULA is displaying (port 0x7FFD bit 3).
    uint8_t active_bank = 5;
    /// The port 0xFF Timex screen-mode latch.
    uint8_t port_ff = 0;
    /// NR 0x26 / NR 0x27 — ULA hardware scroll (`nr_26_ula_scrollx`,
    /// `nr_27_ula_scrolly`).
    uint8_t scroll_x = 0;
    uint8_t scroll_y = 0;
    /// Port 0xFE bits 2:0 (`Ula::get_border()`).
    uint8_t border = 0;
    /// Which of the two ULA palette banks is active. Deliberately the same
    /// answer as `active_ula_palette_bank()`: §4 INS-15 lists both, one as a
    /// scalar and one inside this struct, and a panel reads whichever it
    /// already has. Source —
    /// `nr_43_active_ula_palette`, **NR 0x43 bit 1** (`zxnext.vhd:5393,6825`;
    /// `PaletteManager::active_ula_palette()`). NOT bit 0, which is
    /// `nr_43_ulanext_en`.
    bool second_palette = false;
    /// Is the ULA contributing to the picture — `Ula::ula_enabled()`, i.e.
    /// NR 0x68 bit 7 (the DISABLE bit) inverted.
    bool enabled = true;
};

/// INS-15 — the one published RRRGGGBB → ARGB8888 expansion.
///
/// A declaration here rather than `Renderer::rrrgggbb_to_argb` directly,
/// because `video/renderer.h` pulls `video/ula.h` and `video/lores.h` in and
/// this header's include budget (see the banner) does not stretch that far.
/// B1 implements it by FORWARDING to `Renderer::rrrgggbb_to_argb` — one
/// implementation, two entry points — and B5 pins the agreement over all 256
/// inputs (`Renderer::rrrgggbb_to_argb` is not `constexpr`, so a
/// `static_assert` cannot do it here).
uint32_t rrrgggbb_to_argb(uint8_t rrrgggbb);

/// CAP-01 — the four `--delayed-screenshot-layers` bits (`layer_mask`).
/// Numerically `Renderer::LAYER_*`; `debug_types_check.cpp` asserts it.
constexpr uint8_t LAYER_MASK_ULA     = 0x01;
constexpr uint8_t LAYER_MASK_LAYER2  = 0x02;
constexpr uint8_t LAYER_MASK_SPRITES = 0x04;
constexpr uint8_t LAYER_MASK_TILES   = 0x08;
constexpr uint8_t LAYER_MASK_ALL     = 0x0F;

// ---------------------------------------------------------------------------
// INS-16 / CAP-IN — input
// ---------------------------------------------------------------------------

/// Everything the guest can see of the host's input devices (INS-16) — the one
/// read the #20 recorder samples per frame.
struct InputState {
    /// `matrix[row]`: 5 bits, ACTIVE-LOW (bit N clear = column N pressed),
    /// exactly as `Keyboard::matrix_` holds it.
    std::array<uint8_t, 8> matrix{};
    /// The 16-key extended matrix, ACTIVE-HIGH (bit `id` set = pressed), as NR
    /// 0xB0 / 0xB1 read it back.
    uint16_t ext_keys = 0;
    /// The two connectors' 12-bit button state.
    uint16_t joy_left12  = 0;
    uint16_t joy_right12 = 0;
    /// The composed port 0x1F / 0x37 read values.
    uint8_t port_1f = 0;
    uint8_t port_37 = 0;
};

/// IN-01 / IN-02 — a keyboard matrix position, optionally compound.
///
/// `row2`/`col2` are -1 when there is no second key. The compound form is how
/// CAPS SHIFT (0,0) and SYMBOL SHIFT (7,1) reach the guest — the same shape
/// `Keyboard::AutoKey` uses.
struct MatrixKey {
    int row1 = -1;
    int col1 = -1;
    int row2 = -1;
    int col2 = -1;
    bool compound() const { return row2 >= 0 && col2 >= 0; }
};

/// IN-01 — name → matrix position, the man page's key vocabulary.
///
/// Moved out of `headless_app.cpp`, where it was a file-static, so the two GUI
/// frontends, the DSL and `--delayed-keypress` share ONE table instead of the
/// CLI owning it. Accepts a single alnum character, the punctuation with a
/// well-known SYMBOL SHIFT compound, the named keys
/// (enter/return/space/up/down/left/right) and the explicit `sym+<c>` /
/// `caps+<c>` forms, case-insensitively.
///
/// Returns false for a name it does not know, and on that path `out` is
/// DEFAULT-CONSTRUCTED (all four fields -1), never left as it was. Four callers
/// share this function — the DSL, both GUI frontends and `--delayed-keypress` —
/// and "unspecified on false" is a footgun in a function that many hands call:
/// one caller that forgets to check the bool would inject whatever key the
/// previous call left behind.
bool key_name_to_matrix(const std::string& name, MatrixKey& out);

/// IN-03 — which joystick connector.
enum class JoystickSide : uint8_t { Left = 0, Right = 1 };

// ---------------------------------------------------------------------------
// CAP-01 / CAP-03 / CAP-04 / CAP-ST — capture and state
// ---------------------------------------------------------------------------

/// CAP-01 — screenshot container.
enum class ScreenshotFormat : uint8_t {
    /// 320x256 (or 640x512 in an 80-column mode) ARGB PNG of the composited
    /// picture, honouring `layer_mask`.
    Png = 0,
    /// A 6912-byte `.SCR` of the ULA layer's memory. Incompatible with a
    /// `layer_mask` other than `LAYER_MASK_ALL`.
    Scr,
};

/// ST-01 / CAP-03 — what `save_state_bytes()` / `bookmark_save()` do about a
/// mid-frame call.
enum class SaveStateMode : uint8_t {
    /// Run the `SuspendScope` advance to the next frame boundary and save
    /// there. The frame boundary is not negotiable — only who waits for it is.
    AdvanceToBoundary = 0,
    /// Refuse with `Result::NotAtFrameBoundary`, for a client that cannot
    /// refresh its register cache behind its own back (DeZog).
    RefuseMidFrame,
};

/// ST-03 — what the rewind buffer currently holds — enough to grey a control
/// BEFORE it is clicked, which is why the bounds are given in both domains the
/// two rewind verbs take.
struct RewindRange {
    /// `step_back()` works in master cycles; `rewind_to_frame()` in frames. A
    /// caller needs the bound in the unit it is about to pass.
    uint64_t oldest_cycle = 0;
    uint64_t newest_cycle = 0;
    uint32_t oldest_frame = 0;
    uint32_t newest_frame = 0;
    /// Slots in use, and slots there are.
    size_t depth    = 0;
    size_t capacity = 0;
    /// Bytes per snapshot slot, fixed at construction.
    size_t snapshot_bytes = 0;
};

/// CAP-SYM — which MAP file dialect `load_map()` parses.
enum class MapFormat : uint8_t {
    /// Z88DK linker output — the `; addr` lines only.
    Z88dk = 0,
    /// `SYMBOL = $ADDR`, `;` comments, no metadata filtering.
    Simple,
};

/// INS-20 — one bit per 16-bit PC, set when that address has been executed.
/// 8 KB; handed out by const reference, never copied per call.
using CoverageBits = std::bitset<65536>;

}  // namespace dbg
}  // namespace jnext
