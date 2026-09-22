#include "debug/raster_state.h"

#include "video/timing.h"

namespace {

/// Positive modulus. Every counter here is a hardware wrap-around counter, and
/// the rebases below routinely produce a negative intermediate.
inline int pmod(int v, int m)
{
    if (m <= 0) return 0;
    int r = v % m;
    return (r < 0) ? r + m : r;
}

}  // namespace

RasterState raster_state_at(const VideoTiming& timing,
                            int raw_hc, int raw_vc,
                            uint8_t port_ff_screen_mode,
                            bool ula_shadow_en)
{
    const int ticks_per_line = timing.hc_max() + 1;   // VHDL c_max_hc + 1
    const int lines_per_frame = timing.vc_max() + 1;  // VHDL c_max_vc + 1

    RasterState s;
    s.raw_hc = pmod(raw_hc, ticks_per_line);
    s.raw_vc = pmod(raw_vc, lines_per_frame);

    // ── hc_ula / vc_ula (zxula_timing.vhd:423-453) ──────────────────────────
    //
    //   ula_min_hactive <= c_min_hactive - 12;                        -- :423
    //   ula_max_hc      <= '1' when hc = ula_min_hactive;             -- :424
    //   hc_ula          <= 0 when ula_max_hc else hc_ula + 1;         -- :427-436
    //
    // Both resets are REGISTERED, so hc_ula reads 0 one tick after the
    // combinational compare — at raw hc `c_min_hactive - 11`, which is what
    // VideoTiming::hc_ula_zero_raw_hc() returns (:344 "EVERYTHING BELOW
    // DELAYED ONE PIXEL FROM FRAME COUNTER"; GH #181).
    const int hc_ula_origin = timing.hc_ula_zero_raw_hc();
    s.hc_ula = pmod(s.raw_hc - hc_ula_origin, ticks_per_line);

    // vc_ula is clocked by the SAME `ula_max_hc` pulse (:441-451), so an ULA
    // line starts at raw hc == hc_ula_origin, NOT at raw hc 0: the part of a
    // raw line before that origin still belongs to the previous ULA line.
    // This is the seam GH #257 got wrong on the line-interrupt path.
    const int ula_line = s.raw_vc - ((s.raw_hc < hc_ula_origin) ? 1 : 0);
    const int min_vactive = timing.display_origin().vc;  // VHDL c_min_vactive
    s.vc_ula = pmod(ula_line - min_vactive, lines_per_frame);

    // ── cvc (zxula_timing.vhd:455-470) ──────────────────────────────────────
    // Reloaded from `i_cu_offset` (NR 0x64) at ula_min_vactive, incremented on
    // the same pulse as vc_ula, wrapped at c_max_vc. Read back by NR 0x1E/0x1F
    // (zxnext.vhd:5982-5986) — NOT the raw frame line (GH #16).
    s.cvc = pmod(s.vc_ula + timing.cu_offset(), lines_per_frame);

    // ── phc (zxula_timing.vhd:510-522) ──────────────────────────────────────
    // `phc` is loaded with -48 at raw hc `c_min_hactive - 48` and counts up, so
    // it reaches 0 exactly 12 ticks after hc_ula does — zxula.vhd:43-46 says
    // the same thing from the other side ("0 corresponds to when the system is
    // actually generating pixel 0 ... ULA count i_hc = 0xC").  Reported signed
    // over [-48, ticks_per_line - 49] instead of as a wrapped 9-bit value.
    s.phc = s.hc_ula - 12;
    if (s.phc > ticks_per_line - 49) s.phc -= ticks_per_line;

    // ── region (zxula.vhd:414-415, zxula_timing.vhd:348-357) ────────────────
    // Blanking is tested FIRST and on the RAW counters, because that is the
    // signal that actually blanks the display.  It cannot overlap the paper
    // area on any machine (c_max_hblank is 95 or 63, and paper starts at raw hc
    // c_min_hactive + 1 = 129/137; c_max_vblank is 7 or 15 against a paper
    // origin of raw line 64 or 80), so the ordering is a readability choice,
    // not a tie-break.
    // The compare, not the registered `blank_n` output (:351 vs :352): like
    // `int_position()` and every other position VideoTiming exposes, this is
    // the combinational condition, and the one-tick output delay is not
    // modelled.
    const bool blanking = (s.raw_hc <= timing.max_hblank())
                       || (s.raw_vc <= timing.max_vblank());
    // border_active_v <= i_vc(8) or (i_vc(7) and i_vc(6))   -- zxula.vhd:414
    const bool border_v = (s.vc_ula & 0x100) != 0
                       || ((s.vc_ula & 0x80) != 0 && (s.vc_ula & 0x40) != 0);
    // border_active <= i_phc(8) or border_active_v          -- zxula.vhd:415
    const bool paper = !border_v && s.phc >= 0 && s.phc <= 255;

    s.region = blanking ? RasterRegion::Blanking
             : paper    ? RasterRegion::Paper
                        : RasterRegion::Border;

    // ── ULA fetch (zxula.vhd:226-263, 270-303, 416) ─────────────────────────
    // border_active_ula <= i_hc(8) or border_active_v       -- zxula.vhd:416
    const bool fetching = !border_v && (s.hc_ula & 0x100) == 0;
    if (!fetching) {
        s.fetch = UlaFetch::Idle;
    } else {
        // screen_mode_s <= i_port_ff_reg(2:0) when i_ula_shadow_en = '0'
        //                  else "000"                       -- zxula.vhd:191
        const int screen_mode = ula_shadow_en ? 0 : (port_ff_screen_mode & 0x07);
        // Address phase alternates every 2 ULA ticks: pixel address on
        // i_hc(3:0) = F/3/7/B (held through the following even+odd pair),
        // attribute address on 1/5/9/D (:234-252) — so hc_ula bit 1 selects
        // the class of the byte in flight, and the pbyte/abyte latch schedule
        // at :270-303 pairs with it exactly.
        const bool attribute_slot = (s.hc_ula & 0x02) != 0;
        // Timex hi-colour / hi-res: the attribute slots fetch the second
        // bitmap plane at +0x2000 instead (:238-239, :248-249).
        const bool second_plane = (screen_mode & 0x02) != 0;
        s.fetch = (attribute_slot && !second_plane) ? UlaFetch::Attribute
                                                    : UlaFetch::Bitmap;
    }

    return s;
}

const char* raster_region_name(RasterRegion r)
{
    switch (r) {
        case RasterRegion::Paper:  return "Paper";
        case RasterRegion::Border: return "Border";
        default:                   return "Blanking";
    }
}

const char* ula_fetch_name(UlaFetch f)
{
    switch (f) {
        case UlaFetch::Bitmap:    return "Bitmap";
        case UlaFetch::Attribute: return "Attribute";
        default:                  return "Idle";
    }
}
