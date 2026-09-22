#pragma once

#include <cstdint>

class VideoTiming;

// ---------------------------------------------------------------------------
// RasterState — where the beam is, and what the ULA is fetching (GH #22).
//
// The debugger's answer to "I broke here; had the ULA already read the byte I
// was about to change?".  Everything below is derived from the SAME VideoTiming
// instance the emulator runs on — there is deliberately no second table of
// per-machine raster constants anywhere in the debugger.
//
// FOUR COUNTERS, FOUR DIFFERENT ORIGINS.  Confusing them is the failure mode
// this module exists to remove (GH #16 read NR 0x1E/0x1F as a raw frame line;
// GH #181 fed the Copper a 28 MHz master-cycle count where the VHDL compares a
// 7 MHz `hc_ula`).  Every field below names the VHDL signal it mirrors, and the
// UI labels them with those names.
//
//   raw_hc / raw_vc  VHDL `hc` / `vc`, the FRAME counters
//                    (zxula_timing.vhd:314-341).  hc 0..c_max_hc, vc 0..c_max_vc,
//                    origin at the top-left of the whole frame including
//                    blanking.  This is what jnext's Emulator::paused_hc() /
//                    paused_vc() and VideoTiming::pos() hold.
//
//   hc_ula / vc_ula  VHDL `o_hc_ula` / `o_vc_ula` (zxula_timing.vhd:423-453),
//                    which zxnext.vhd:4443-4444 wires into the ULA as its
//                    `i_hc` / `i_vc` (from :6737-6738).  DISPLAY-relative: both
//                    read 0 at the ULA's own prefetch origin — raw hc
//                    `c_min_hactive - 11`, raw vc `c_min_vactive`.  The fetch
//                    schedule, the contention window and the Copper's WAIT
//                    comparison all live in THIS domain, not the raw one.
//
//   cvc              VHDL `o_vc_cu` (zxula_timing.vhd:455-470): vc_ula plus the
//                    NR 0x64 copper offset, wrapping at c_max_vc.  THIS is what
//                    NR 0x1E/0x1F read back (zxnext.vhd:5982-5986) and what the
//                    line interrupt compares against (zxula_timing.vhd:577).
//
//   phc              VHDL `o_phc` (zxula_timing.vhd:510-522), the "practical"
//                    counter: phc == 0 is the instant pixel 0 of the line is
//                    actually generated, which zxula.vhd:43-46 notes is ULA
//                    count `i_hc = 0xC`.  Reported SIGNED here, in [-48, ...],
//                    exactly as the VHDL loads it (`phc <= "111010000"` = -48,
//                    :515), so the left border reads as negative pixel columns
//                    rather than as 9-bit wrap-around.
//
// The 12-tick gap between `hc_ula` and `phc` is not bookkeeping: it is the
// ULA's prefetch lead.  At the start of every display line the ULA is already
// fetching (hc_ula 0..11) while the beam is still painting the left border
// (phc -12..-1), and at the end of the line it has stopped fetching while the
// last 12 pixels are shifted out.  `region` and `fetch` below are therefore
// INDEPENDENT, and the pair is the whole point of the indicator.
// ---------------------------------------------------------------------------

/// Which part of the frame the beam is in.
///
/// VHDL, in the order tested here:
///   * `Blanking` — `blank_n <= '0' when (hc <= c_max_hblank) or
///     (vc <= c_max_vblank)` (zxula_timing.vhd:348-357).  Both c_min_*blank are
///     0 on every machine, so blanking is the CLOSED interval [0, c_max_*blank]
///     of the RAW counters.  Sync sits inside it and is not broken out.
///   * `Paper` — `border_active = '0'`, i.e. `i_phc(8) or border_active_v`
///     false (zxula.vhd:414-415): phc in [0, 255] and vc_ula in [0, 191].
///   * `Border` — anything else: visible, but not part of the 256x192 area.
enum class RasterRegion { Blanking, Border, Paper };

/// What the ULA is pulling out of display RAM at this instant.
///
/// The read schedule repeats every 4 ULA ticks (zxula.vhd:226-263): the address
/// register `vram_a` is reassigned on every ODD `i_hc(3:0)` and holds for the
/// following even+odd pair, while `vram_rd` is asserted on the odd tick of that
/// pair.  Pixel addresses are set on `i_hc(3:0)` = F/3/7/B and attribute
/// addresses on 1/5/9/D, so the byte in flight is a BITMAP byte while
/// `hc_ula(1) = '0'` and an ATTRIBUTE byte while `hc_ula(1) = '1'`.  The latch
/// schedule at zxula.vhd:270-303 confirms the pairing: pbyte* are captured at
/// `i_hc(3:0)` 1/5/9/D, abyte* at 3/7/B/F.
///
/// `Idle` is `border_active_ula = '1'` — `i_hc(8) or border_active_v`
/// (zxula.vhd:416), the same gate that disables the floating bus (:311-315,
/// :573) and substitutes the border colour for the attribute (:426-430).  The
/// Next's FPGA does keep cycling BRAM outside that window (the entity comment
/// at zxula.vhd:48-51 says so explicitly, because dual-port BRAM has bandwidth
/// to spare), but nothing consumes the result, so "idle" is the honest answer
/// to "is the ULA reading display data right now".
///
/// In Timex hi-colour / hi-res (`screen_mode(1) = '1'`) the attribute slots
/// fetch a SECOND BITMAP PLANE at +0x2000 instead of attributes
/// (zxula.vhd:238-239, 248-249), so those ticks report `Bitmap`.
enum class UlaFetch { Idle, Bitmap, Attribute };

struct RasterState {
    int raw_hc = 0;     ///< VHDL `hc`      — frame counter, 0..c_max_hc
    int raw_vc = 0;     ///< VHDL `vc`      — frame counter, 0..c_max_vc
    int hc_ula = 0;     ///< VHDL `o_hc_ula`— 0 at raw hc c_min_hactive - 11
    int vc_ula = 0;     ///< VHDL `o_vc_ula`— 0 on raw line c_min_vactive
    int cvc    = 0;     ///< VHDL `o_vc_cu` — vc_ula + NR 0x64; NR 0x1E/0x1F
    int phc    = 0;     ///< VHDL `o_phc`   — signed pixel column, phc 0 = pixel 0

    RasterRegion region = RasterRegion::Blanking;
    UlaFetch     fetch  = UlaFetch::Idle;

    /// True while the beam is inside the 256x192 area, i.e. `region == Paper`.
    /// `phc` is then the pixel column (0..255) and `vc_ula` the pixel row
    /// (0..191).
    bool in_paper() const { return region == RasterRegion::Paper; }
};

/// Derive the full raster state from the raw frame counters.
///
/// @param timing              the live VideoTiming — the ONLY source of the
///                            per-machine constants used here.
/// @param raw_hc              VHDL `hc`, 0..VideoTiming::hc_max().
/// @param raw_vc              VHDL `vc`, 0..VideoTiming::vc_max().
/// @param port_ff_screen_mode port 0xFF bits 2:0 (VHDL `i_port_ff_reg(2:0)`).
/// @param ula_shadow_en       port 0x7FFD bit 3 (VHDL `i_ula_shadow_en`), which
///                            forces screen_mode to "000" (zxula.vhd:191).
///
/// Out-of-range counters are folded into the frame rather than rejected: the
/// debugger must never be the thing that crashes on a machine-type switch
/// mid-frame.
RasterState raster_state_at(const VideoTiming& timing,
                            int raw_hc, int raw_vc,
                            uint8_t port_ff_screen_mode,
                            bool ula_shadow_en);

/// Short display names, so the panel and the tests agree on the wording.
const char* raster_region_name(RasterRegion r);
const char* ula_fetch_name(UlaFetch f);
