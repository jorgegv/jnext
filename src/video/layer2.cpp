#include "video/layer2.h"
#include "video/palette.h"
#include "memory/ram.h"
#include "core/log.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void Layer2::reset(bool hard)
{
    active_bank_    = 8;
    shadow_bank_    = 11;
    scroll_x_       = 0;
    scroll_y_       = 0;
    palette_offset_ = 0;
    resolution_     = 0;
    enabled_        = false;
    // VHDL zxnext.vhd:4959-4962 NR 0x18 (Layer 2 clip) reset defaults:
    //   clip_x1=0x00, clip_x2=0xFF, clip_y1=0x00, clip_y2=0xBF (191).
    clip_x1_        = 0x00;
    clip_x2_        = 0xFF;
    clip_y1_        = 0x00;
    clip_y2_        = 0xBF;

    // GH #263 — a soft reset records itself in every log at the current line
    // instead of wiping them (see the header).
    if (!hard) {
        log_scroll_change();
        log_clip_change();
        log_bank_change();
        log_enable_change();
        log_nr70_change();
        return;
    }

    // Per-scanline change log cleared. Baseline reset to current (zero)
    // state; start_frame() will re-snapshot from the live values at the
    // next frame boundary.
    change_count_      = 0;
    current_line_      = 0;
    current_hpos_      = kHposLineStart;
    render_cursor_     = 0;
    overflow_warned_   = false;
    baseline_scroll_x_ = 0;
    baseline_scroll_y_ = 0;

    // GH #270 — the per-line render segments are derived state; drop them
    // so nothing can render from a list built before the reset.
    line_segment_count_       = 0;
    segment_overflow_warned_  = false;

    clip_change_count_     = 0;
    clip_render_cursor_    = 0;
    clip_overflow_warned_  = false;
    baseline_clip_x1_      = 0x00;
    baseline_clip_x2_      = 0xFF;
    baseline_clip_y1_      = 0x00;
    baseline_clip_y2_      = 0xBF;

    bank_change_count_     = 0;
    bank_render_cursor_    = 0;
    bank_overflow_warned_  = false;
    baseline_active_bank_  = 8;
    baseline_shadow_bank_  = 11;

    enable_change_count_    = 0;
    enable_render_cursor_   = 0;
    enable_overflow_warned_ = false;
    baseline_enabled_       = false;

    nr70_change_count_       = 0;
    nr70_render_cursor_      = 0;
    nr70_overflow_warned_    = false;
    baseline_resolution_     = 0;
    baseline_palette_offset_ = 0;
}

// ---------------------------------------------------------------------------
// Per-scanline change log
// ---------------------------------------------------------------------------

void Layer2::log_scroll_change()
{
    if (change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!overflow_warned_) {
            Log::video()->warn(
                "Layer2: scroll change-log full at line {} (cap {} per "
                "frame); further NR 0x16/0x17/0x71 writes this frame "
                "will not be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            overflow_warned_ = true;
        }
        return;
    }
    change_log_[change_count_++] = ScrollChange{
        current_line_,
        current_hpos_,
        scroll_x_,
        scroll_y_,
    };
}

void Layer2::log_clip_change()
{
    if (clip_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!clip_overflow_warned_) {
            Log::video()->warn(
                "Layer2: clip change-log full at line {} (cap {} per "
                "frame); further NR 0x18 writes this frame will not be "
                "per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            clip_overflow_warned_ = true;
        }
        return;
    }
    clip_change_log_[clip_change_count_++] = ClipChange{
        current_line_, clip_x1_, clip_x2_, clip_y1_, clip_y2_,
    };
}

void Layer2::log_bank_change()
{
    if (bank_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!bank_overflow_warned_) {
            Log::video()->warn(
                "Layer2: bank change-log full at line {} (cap {} per "
                "frame); further NR 0x12/0x13 writes this frame will "
                "not be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            bank_overflow_warned_ = true;
        }
        return;
    }
    bank_change_log_[bank_change_count_++] = BankChange{
        current_line_, current_hpos_, active_bank_, shadow_bank_,
    };
}

void Layer2::log_enable_change()
{
    if (enable_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!enable_overflow_warned_) {
            Log::video()->warn(
                "Layer2: enable change-log full at line {} (cap {} per "
                "frame); further set_enabled writes this frame will "
                "not be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            enable_overflow_warned_ = true;
        }
        return;
    }
    enable_change_log_[enable_change_count_++] = EnableChange{
        current_line_, enabled_,
    };
}

void Layer2::log_nr70_change()
{
    if (nr70_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!nr70_overflow_warned_) {
            Log::video()->warn(
                "Layer2: NR 0x70 change-log full at line {} (cap {} per "
                "frame); further NR 0x70 writes this frame will not be "
                "per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            nr70_overflow_warned_ = true;
        }
        return;
    }
    nr70_change_log_[nr70_change_count_++] = Nr70Change{
        current_line_, resolution_, palette_offset_,
    };
}

void Layer2::start_frame()
{
    baseline_scroll_x_ = scroll_x_;
    baseline_scroll_y_ = scroll_y_;
    change_count_      = 0;
    render_cursor_     = 0;
    current_line_      = 0;
    current_hpos_      = kHposLineStart;
    overflow_warned_   = false;

    baseline_clip_x1_      = clip_x1_;
    baseline_clip_x2_      = clip_x2_;
    baseline_clip_y1_      = clip_y1_;
    baseline_clip_y2_      = clip_y2_;
    clip_change_count_     = 0;
    clip_render_cursor_    = 0;
    clip_overflow_warned_  = false;

    baseline_active_bank_  = active_bank_;
    baseline_shadow_bank_  = shadow_bank_;
    bank_change_count_     = 0;
    bank_render_cursor_    = 0;
    bank_overflow_warned_  = false;

    baseline_enabled_       = enabled_;
    enable_change_count_    = 0;
    enable_render_cursor_   = 0;
    enable_overflow_warned_ = false;

    baseline_resolution_     = resolution_;
    baseline_palette_offset_ = palette_offset_;
    nr70_change_count_       = 0;
    nr70_render_cursor_      = 0;
    nr70_overflow_warned_    = false;
}

void Layer2::rewind_to_baseline()
{
    scroll_x_              = baseline_scroll_x_;
    scroll_y_              = baseline_scroll_y_;
    render_cursor_         = 0;

    clip_x1_               = baseline_clip_x1_;
    clip_x2_               = baseline_clip_x2_;
    clip_y1_               = baseline_clip_y1_;
    clip_y2_               = baseline_clip_y2_;
    clip_render_cursor_    = 0;

    active_bank_           = baseline_active_bank_;
    shadow_bank_           = baseline_shadow_bank_;
    bank_render_cursor_    = 0;

    enabled_               = baseline_enabled_;
    enable_render_cursor_  = 0;

    resolution_            = baseline_resolution_;
    palette_offset_        = baseline_palette_offset_;
    nr70_render_cursor_    = 0;

    // GH #270 — a segment list left over from the previous frame must not
    // outlive the rewind: render_scanline would draw row 0 from it before
    // apply_changes_for_line(0) has rebuilt it.
    line_segment_count_ = 0;
}

// ---------------------------------------------------------------------------
// Mid-line render segments (GH #270)
// ---------------------------------------------------------------------------
//
// hpos -> first affected SOURCE column, derived from the VHDL.
//
// `hpos` is `raw_hc - c_min_hactive` (see Layer2::set_current_hpos), so the
// derivation below is stated in raw `hc` and then rebased.
//
//  1. The NextREG file and Layer 2 both run off the same counters:
//       zxula_timing.vhd:476-520 — the 7 MHz counters are reset from the raw
//       frame counter `hc`:
//           wide_hactive <= '1' when hc = c_min_hactive - 48
//           phc  <= -48   (256-mode practical counter)  on that pulse
//           whc  <= -16   (320/640-mode wide counter)   on that pulse
//       Both are REGISTERED, so they take the reload value on the edge AFTER
//       the compare, i.e. at raw hc = c_min_hactive - 47. Counting up from
//       there:
//           phc == P  <=>  hc == c_min_hactive + P + 1
//           whc == W  <=>  hc == c_min_hactive + W - 31
//       (and therefore whc = phc + 32, the 32-column wide-mode overscan on
//        each side of the 256-wide area.)
//
//  2. layer2.vhd:145-148 picks the counter and looks ONE column ahead:
//           hc <= i_phc when narrow else i_whc;
//           hc_eff <= hc + 1;
//     `hc_eff` is the SOURCE column whose address is generated during that
//     7 MHz period — and it is exactly the `x` this renderer loops over
//     (clip and scroll are both applied to it, layer2.vhd:154,167).
//
//  3. The bank / scroll inputs are resampled one 7 MHz period before use:
//           layer2.vhd:110-122 — layer2_active_bank_q, layer2_scroll_x_q,
//           layer2_scroll_y_q <= their NR inputs, on i_CLK_7.
//     So a NextREG value that becomes visible during 7 MHz period Q is first
//     USED for an address in period Q+1, i.e. for source column Q+2.
//
//  4. When does the value become visible? For a Copper MOVE the VHDL takes
//     three further 28 MHz cycles after the MOVE cycle — copper_dout_s is
//     registered (copper.vhd:101), copper_requester_d delays it one cycle and
//     copper_req one more (zxnext.vhd:4709-4731), and the NR write process
//     itself is clocked (nr_wr_en, zxnext.vhd:4775). A MOVE issued in the
//     FIRST 28 MHz sub-cycle of 7 MHz period H therefore lands the new NR
//     value on the CLK_7 edge that opens period H+1 — the same edge the
//     resample in (3) fires on, so the resample still captures the OLD value
//     and `layer2_*_q` only changes at the edge opening period H+2.
//
//     Net: value written during 7 MHz period H is first USED for source
//     column (H+2) + 1 - 12 ... expressed directly in raw hc, with
//     phc = hc - c_min_hactive - 1:
//
//         first source column (narrow) = hpos + 2
//         first source column (wide)   = hpos + 34      (= narrow + 32)
//
//     Checked against MAME 0.289 (`tbblue`) in GH #270: a Copper
//     WAIT(line, hpos=8) + MOVE NR 0x12 has threshold (8<<3)+12 = 76
//     (copper.vhd:94), satisfied at hc_ula = 76, so raw hc = 76 + 125 = 201
//     and hpos = 201 - 136 = 65 -> first affected column 67. MAME measures
//     the boundary at display pixel 67.
static inline int seg_first_col(int16_t hpos, bool wide)
{
    return static_cast<int>(hpos) + (wide ? 34 : 2);
}

int Layer2::segment_first_column(size_t i, bool wide) const
{
    if (i >= line_segment_count_) return 0;
    return seg_first_col(line_segments_[i].hpos, wide);
}

void Layer2::begin_line_segments()
{
    line_segments_[0] = LineSegment{
        kHposLineStart, active_bank_, scroll_y_, scroll_x_,
    };
    line_segment_count_ = 1;
}

void Layer2::push_line_segment(int16_t hpos)
{
    LineSegment& last = line_segments_[line_segment_count_ - 1];

    // Keep the list monotone. An entry can legitimately arrive with a
    // SMALLER hpos than its predecessor: an instruction that straddles a
    // raw-line boundary has its whole Copper window replayed under the
    // line tag that was current when the scheduler last fired, so the tail
    // of that window carries next-line columns. Clamping it forward keeps
    // every span non-empty and makes the stray write apply no earlier than
    // the previous one — strictly better than the pre-GH-#270 behaviour,
    // which let it repaint the whole line.
    if (hpos < last.hpos) hpos = last.hpos;

    // Writes sharing a column (the usual `MOVE bank` + `MOVE scroll` pair)
    // are one segment, which is also what keeps the cap unreachable.
    if (hpos == last.hpos) {
        last.active_bank = active_bank_;
        last.scroll_x    = scroll_x_;
        last.scroll_y    = scroll_y_;
        return;
    }

    if (line_segment_count_ >= MAX_SEGMENTS_PER_LINE) {
        if (!segment_overflow_warned_) {
            Log::video()->warn(
                "Layer2: mid-line segment list full at line {} (cap {}); "
                "the rest of this scanline renders from the last segment.",
                current_line_, MAX_SEGMENTS_PER_LINE);
            segment_overflow_warned_ = true;
        }
        last.active_bank = active_bank_;
        last.scroll_x    = scroll_x_;
        last.scroll_y    = scroll_y_;
        return;
    }

    line_segments_[line_segment_count_++] = LineSegment{
        hpos, active_bank_, scroll_y_, scroll_x_,
    };
}

void Layer2::apply_changes_for_line(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);

    // GH #270 — the scroll and bank logs are replayed TOGETHER, ordered by
    // the column each write landed at, because they feed the same set of
    // render segments. The interleaving does not change the end-of-line
    // live state (the two logs touch disjoint registers), only which state
    // each span of the line is drawn with.
    begin_line_segments();
    for (;;) {
        const bool has_scroll = render_cursor_ < change_count_
                             && change_log_[render_cursor_].line == lt;
        const bool has_bank   = bank_render_cursor_ < bank_change_count_
                             && bank_change_log_[bank_render_cursor_].line == lt;
        if (!has_scroll && !has_bank) break;

        const bool take_scroll =
            !has_bank
            || (has_scroll
                && change_log_[render_cursor_].hpos
                       <= bank_change_log_[bank_render_cursor_].hpos);

        int16_t hpos;
        if (take_scroll) {
            const auto& c = change_log_[render_cursor_++];
            scroll_x_ = c.scroll_x;
            scroll_y_ = c.scroll_y;
            hpos      = c.hpos;
        } else {
            const auto& c = bank_change_log_[bank_render_cursor_++];
            active_bank_ = c.active_bank;
            shadow_bank_ = c.shadow_bank;
            hpos         = c.hpos;
        }
        push_line_segment(hpos);
    }

    while (clip_render_cursor_ < clip_change_count_
        && clip_change_log_[clip_render_cursor_].line == lt) {
        const auto& c = clip_change_log_[clip_render_cursor_++];
        clip_x1_ = c.x1;
        clip_x2_ = c.x2;
        clip_y1_ = c.y1;
        clip_y2_ = c.y2;
    }

    while (enable_render_cursor_ < enable_change_count_
        && enable_change_log_[enable_render_cursor_].line == lt) {
        const auto& c = enable_change_log_[enable_render_cursor_++];
        enabled_ = c.enabled;
    }

    while (nr70_render_cursor_ < nr70_change_count_
        && nr70_change_log_[nr70_render_cursor_].line == lt) {
        const auto& c = nr70_change_log_[nr70_render_cursor_++];
        resolution_     = c.resolution;
        palette_offset_ = c.palette_offset;
    }
}

void Layer2::flush_remaining_changes()
{
    // Drain entries the per-line render loop did not reach (line >=
    // FB_HEIGHT, vblank). See Renderer::render_frame for the rationale —
    // this is the same pattern as PaletteManager::flush_remaining_changes
    // and Tilemap::flush_remaining_nr6b_changes.
    while (render_cursor_ < change_count_) {
        const auto& c = change_log_[render_cursor_++];
        scroll_x_ = c.scroll_x;
        scroll_y_ = c.scroll_y;
    }
    while (clip_render_cursor_ < clip_change_count_) {
        const auto& c = clip_change_log_[clip_render_cursor_++];
        clip_x1_ = c.x1;
        clip_x2_ = c.x2;
        clip_y1_ = c.y1;
        clip_y2_ = c.y2;
    }
    while (bank_render_cursor_ < bank_change_count_) {
        const auto& c = bank_change_log_[bank_render_cursor_++];
        active_bank_ = c.active_bank;
        shadow_bank_ = c.shadow_bank;
    }
    while (enable_render_cursor_ < enable_change_count_) {
        const auto& c = enable_change_log_[enable_render_cursor_++];
        enabled_ = c.enabled;
    }
    while (nr70_render_cursor_ < nr70_change_count_) {
        const auto& c = nr70_change_log_[nr70_render_cursor_++];
        resolution_     = c.resolution;
        palette_offset_ = c.palette_offset;
    }
}

// ---------------------------------------------------------------------------
// NextREG 0x70 — Layer 2 control
// ---------------------------------------------------------------------------

void Layer2::set_control(uint8_t val)
{
    resolution_     = (val >> 4) & 0x03;
    palette_offset_ = val & 0x0F;
    log_nr70_change();
}

// ---------------------------------------------------------------------------
// Render one scanline
// ---------------------------------------------------------------------------
//
// VHDL reference: layer2.vhd
//
// Memory address generation:
//   256x192:  addr = y[7:0] & x[7:0]         (row-major)
//   320x256:  addr = x[8:0] & y[7:0]         (column-major, 8bpp)
//   640x256:  addr = x[8:0] & y[7:0]         (column-major, 4bpp, 2px/byte)
//
// Bank mapping:
//   layer2_bank_eff = ((0 & bank[6:4]) + 1) & bank[3:0]
//   layer2_addr_eff = (bank_eff + addr[16:14]) & addr[13:0]
//   Physical RAM offset = layer2_addr_eff * 2 bytes (SRAM words)
//   But in our emulator, RAM is byte-addressed from bank base.

static inline uint32_t compute_ram_addr(uint8_t active_bank, uint32_t l2_addr,
                                        bool rom_in_sram)
{
    // VHDL layer2.vhd:172 applies an UNCONDITIONAL `bank_eff = active_bank + 16`
    // (encoded as `+1` in the high nibble of layer2_bank_eff). This is the
    // fixed SRAM-layout offset for ZX RAM (which starts at SRAM 0x040000 =
    // 16K-bank 16). The transform fires combinatorially on EVERY pixel
    // fetch with no guard, no enable, no exception. The 5 contiguous 16K
    // pages for 320×256 mode then live at SRAM banks {N+16, N+17, N+18,
    // N+19, N+20} for any NR $12 value N.
    //
    // Bug history: a previous form gated the +16 with `final_bank < 16`
    // (and added bank-5 / bank-7-lower exceptions that are CPU-port
    // concerns and don't apply to the pixel-fetch path). For NR $12 ≥ 11
    // (e.g. demo defaults to bank 14), sub-banks 2..4 produce
    // pre-shift `final_bank ≥ 16`, failing the guard, leaving them at
    // physical SRAM banks 16..18 = ZX RAM banks 0..2 (CPU code/heap) →
    // animated-noise corruption on right half of 320×256 / 640×256.
    // Found via odemo.nex column-128-boundary symptom, 2026-05-16.
    int bank_16k = active_bank;
    int sub_bank = static_cast<int>(l2_addr >> 14);      // which 16K chunk (0-4)
    int offset   = static_cast<int>(l2_addr & 0x3FFF);   // offset within 16K
    int final_bank = bank_16k + sub_bank;
    if (rom_in_sram) final_bank += 16;                   // VHDL layer2.vhd:172
    return static_cast<uint32_t>(final_bank * 16384 + offset);
}

void Layer2::render_scanline_debug(uint32_t* dst, int row, const Ram& ram,
                                   const PaletteManager& palette, uint8_t bank,
                                   uint8_t transparent_rgb,
                                   bool rom_in_sram,
                                   bool palette_bank_second)
{
    const bool saved_enabled = enabled_;
    const uint8_t saved_bank = active_bank_;
    enabled_      = true;
    active_bank_  = bank;
    // GH #270 — this view FORCES one bank for the whole row, so the
    // mid-line segment list (which carries per-segment banks) would
    // contradict it. Suppress it for the duration of the call: with no
    // segments render_scanline falls back to the live registers, which is
    // exactly what this function has always drawn.
    const size_t saved_segments = line_segment_count_;
    line_segment_count_ = 0;
    // Debugger view doesn't need per-pixel priority info — pass nullptr.
    // transparent_rgb and palette_bank_second come from the CALLER's
    // per-line replay — see the doc comments in layer2.h (Task 46, GH #163).
    render_scanline(dst, row, ram, palette, transparent_rgb,
                    rom_in_sram, /*priority_dst=*/nullptr,
                    palette_bank_second);
    line_segment_count_ = saved_segments;
    enabled_      = saved_enabled;
    active_bank_  = saved_bank;
}

void Layer2::render_scanline(uint32_t* dst, int row, const Ram& ram,
                             const PaletteManager& palette,
                             uint8_t transparent_rgb,
                             bool rom_in_sram,
                             bool* priority_dst,
                             bool palette_bank_second) const
{
    if (!enabled_)
        return;

    // VHDL transparency: compares the 8-bit RRRGGGBB palette output
    // (NOT the raw pixel index) against the global transparency colour
    // (NextREG 0x14).  See zxnext.vhd line 7121. `transparent_rgb` is the
    // caller-supplied reference — see the parameter doc in layer2.h
    // (Task 46) for why this must be the caller's per-line snapshot, not
    // a live NextREG read.
    uint8_t transp_rgb = transparent_rgb;

    if (resolution_ == 0) {
        // ---------------------------------------------------------------
        // 256x192 @ 8bpp (row-major)
        // ---------------------------------------------------------------
        // row is a framebuffer row (0-255). Display area is rows 32-223.
        // G104 Phase 3: emit at canonical 640-grid. Each source pixel
        // (256 across) is pixel-doubled into two adjacent destination
        // cells, spanning the display strip [DISP_X..DISP_X+512). VHDL
        // layer2.vhd at narrow res samples at 7 MHz and the compositor
        // re-samples the layer at 14 MHz; the doubling here matches that
        // 1-pixel-into-2-clocks expansion.
        static constexpr int DISP_Y = 32;
        static constexpr int DISP_X = 64;
        int y = row - DISP_Y;
        if (y < 0 || y >= 192)
            return;

        // Clip Y check on DESTINATION row (pre-scroll), per VHDL
        // layer2.vhd:167 — clip uses `vc_eff`, not `y_pre`.
        if (y < clip_y1_ || y > clip_y2_)
            return;

        // GH #270 — draw the line as one span per mid-line segment. With
        // no mid-line write there is exactly one span covering 0..255 and
        // the emitted pixels are identical to the pre-segmentation loop.
        const size_t nseg = line_segment_count_;
        size_t seg = 0;
        int    x   = 0;
        while (x < 256) {
            uint8_t  seg_bank;
            uint16_t seg_sx;
            uint8_t  seg_sy;
            int      x_end;
            if (nseg == 0) {
                // No replay has run (direct render_scanline callers: unit
                // tests, render_scanline_debug) — use the live registers.
                seg_bank = active_bank_;
                seg_sx   = scroll_x_;
                seg_sy   = scroll_y_;
                x_end    = 256;
            } else {
                while (seg + 1 < nseg
                    && seg_first_col(line_segments_[seg + 1].hpos, false) <= x)
                    ++seg;
                const LineSegment& sg = line_segments_[seg];
                seg_bank = sg.active_bank;
                seg_sx   = sg.scroll_x;
                seg_sy   = sg.scroll_y;
                x_end    = (seg + 1 < nseg)
                         ? seg_first_col(line_segments_[seg + 1].hpos, false)
                         : 256;
                if (x_end > 256) x_end = 256;
            }

            // Y scroll wraps at 192. Per-span because NR 0x17 is one of the
            // registers a mid-line Copper MOVE can change.
            int src_y = (y + seg_sy) % 192;
            uint32_t l2_addr_base = static_cast<uint32_t>(src_y) * 256;

            for (; x < x_end; ++x) {
                // Clip X check on DESTINATION column (pre-scroll), per VHDL
                // layer2.vhd:167 — clip uses `hc_eff`, not `x_pre`. Without
                // this, the 8-pixel "clipped band" wanders through the
                // display as L2 scrolls (parallax.nex bottom band exposed
                // it: side gutters that should be black showed graphics).
                // VHDL clip space at narrow res is 9-bit but the high bit is
                // 0 (clip_x1_q = '0' & i_clip_x1, layer2.vhd:130) so direct
                // 8-bit comparison against x (0..255) is faithful.
                if (x < clip_x1_ || x > clip_x2_)
                    continue;

                int src_x = (x + (seg_sx & 0xFF)) & 0xFF;

                uint32_t l2_addr = l2_addr_base + src_x;
                uint32_t ram_addr = compute_ram_addr(seg_bank, l2_addr, rom_in_sram);
                uint8_t pixel = ram.read(ram_addr);

                uint8_t colour_idx = static_cast<uint8_t>(
                    ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

                // Transparency: compare palette RRRGGGBB against global transparent colour.
                if (palette.layer2_rgb8(palette_bank_second, colour_idx) == transp_rgb)
                    continue;

                // Pixel-double: each 256-mode source pixel writes two
                // 640-grid cells (matches VHDL's narrow-res 7 MHz output
                // re-sampled at the compositor's 14 MHz pixel clock).
                uint32_t argb = palette.layer2_colour(palette_bank_second, colour_idx);
                dst[DISP_X + 2 * x]     = argb;
                dst[DISP_X + 2 * x + 1] = argb;
                // VHDL zxnext.vhd:7050 — palette bit 15 (NR 0x44 b7) drives
                // layer2_priority_2 per-pixel for opaque L2 pixels. Compositor
                // uses this to promote L2 above sprites (zxnext.vhd:7220).
                if (priority_dst) {
                    const bool prio = palette.layer2_priority_high(palette_bank_second, colour_idx);
                    priority_dst[DISP_X + 2 * x]     = prio;
                    priority_dst[DISP_X + 2 * x + 1] = prio;
                }
            }
        }
    }
    else if (resolution_ == 1) {
        // ---------------------------------------------------------------
        // 320x256 @ 8bpp (column-major: addr = x * 256 + y)
        // ---------------------------------------------------------------
        // G104 Phase 3: 320 source pixels → 640 emitted (each doubled).
        // The 320-mode source covers the entire 640-wide framebuffer
        // (no border on the sides) — VHDL layer2.vhd:164 marks the
        // wide mode active over hc_eff = 0..319.
        if (row < 0 || row >= 256)
            return;

        // Clip Y on DESTINATION row (pre-scroll), per VHDL layer2.vhd:167.
        if (row < clip_y1_ || row > clip_y2_)
            return;

        // VHDL clip for wide mode: clip_x1_q = i_clip_x1 & '0',
        //                          clip_x2_q = i_clip_x2 & '1'
        // (layer2.vhd:133-134). 9-bit register space spans 0..511; the
        // 8-bit clip register is therefore in units of "2 source pixels"
        // and `<<1` maps it onto the source column index x ∈ 0..319.
        // hc_eff (the per-pixel comparison signal at line 167) traverses
        // the same 0..319 source space in 320-mode, so direct comparison
        // against x is VHDL-faithful.
        uint16_t clip_x1_eff = static_cast<uint16_t>(clip_x1_) << 1;
        uint16_t clip_x2_eff = (static_cast<uint16_t>(clip_x2_) << 1) | 1;

        // GH #270 — one span per mid-line segment; see the narrow branch.
        const size_t nseg = line_segment_count_;
        size_t seg = 0;
        int    x   = 0;
        while (x < 320) {
            uint8_t  seg_bank;
            uint16_t seg_sx;
            uint8_t  seg_sy;
            int      x_end;
            if (nseg == 0) {
                seg_bank = active_bank_;
                seg_sx   = scroll_x_;
                seg_sy   = scroll_y_;
                x_end    = 320;
            } else {
                while (seg + 1 < nseg
                    && seg_first_col(line_segments_[seg + 1].hpos, true) <= x)
                    ++seg;
                const LineSegment& sg = line_segments_[seg];
                seg_bank = sg.active_bank;
                seg_sx   = sg.scroll_x;
                seg_sy   = sg.scroll_y;
                x_end    = (seg + 1 < nseg)
                         ? seg_first_col(line_segments_[seg + 1].hpos, true)
                         : 320;
                if (x_end > 320) x_end = 320;
            }

            // Y scroll wraps at 256 (natural 8-bit wrap).
            uint8_t src_y = static_cast<uint8_t>(row + seg_sy);

            for (; x < x_end; ++x) {
                // Clip X on DESTINATION column (pre-scroll), per VHDL
                // layer2.vhd:167.
                if (x < clip_x1_eff || x > clip_x2_eff)
                    continue;

                // X scroll with wrap at 320.
                int src_x_pre = x + (seg_sx & 0x1FF);
                int src_x = (src_x_pre >= 320) ? (src_x_pre - 320) : src_x_pre;

                // Column-major: addr = x * 256 + y (17-bit).
                uint32_t l2_addr = static_cast<uint32_t>(src_x) * 256 + src_y;
                uint32_t ram_addr = compute_ram_addr(seg_bank, l2_addr, rom_in_sram);
                uint8_t pixel = ram.read(ram_addr);

                uint8_t colour_idx = static_cast<uint8_t>(
                    ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

                if (palette.layer2_rgb8(palette_bank_second, colour_idx) == transp_rgb)
                    continue;

                // Pixel-double: 320 source → 640 framebuffer cells.
                uint32_t argb = palette.layer2_colour(palette_bank_second, colour_idx);
                dst[2 * x]     = argb;
                dst[2 * x + 1] = argb;
                // VHDL zxnext.vhd:7050 — see narrow-mode comment above.
                if (priority_dst) {
                    const bool prio = palette.layer2_priority_high(palette_bank_second, colour_idx);
                    priority_dst[2 * x]     = prio;
                    priority_dst[2 * x + 1] = prio;
                }
            }
        }
    }
    else {
        // ---------------------------------------------------------------
        // 640x256 @ 4bpp (column-major: addr = x * 256 + y, 2 px/byte)
        // ---------------------------------------------------------------
        // resolution_ == 2 or 3 both select this mode (VHDL: resolution(1)='1').
        // G104 Phase 3: native 640-emit only; the legacy 320 downsampled
        // fallback branch is gone.
        if (row < 0 || row >= 256)
            return;

        // Clip Y on DESTINATION row (pre-scroll), per VHDL layer2.vhd:167.
        if (row < clip_y1_ || row > clip_y2_)
            return;

        // Wide-mode clip (layer2.vhd:133-134): 9-bit, 0..511 grid.
        uint16_t clip_x1_eff = static_cast<uint16_t>(clip_x1_) << 1;
        uint16_t clip_x2_eff = (static_cast<uint16_t>(clip_x2_) << 1) | 1;

        // Each memory address holds 2 horizontal pixels (left = high
        // nibble, right = low nibble). Iterate 320 bytes; emit two
        // adjacent destination cells per byte for the canonical 640.
        //
        // GH #270 — one span per mid-line segment; see the narrow branch.
        // The byte column IS the VHDL `hc_eff` in this mode too
        // (layer2.vhd:147 selects i_whc for every wide resolution), so the
        // segment boundary lands on the same column index as 320-mode.
        const size_t nseg = line_segment_count_;
        size_t seg = 0;
        int    col = 0;
        while (col < 320) {
            uint8_t  seg_bank;
            uint16_t seg_sx;
            uint8_t  seg_sy;
            int      col_end;
            if (nseg == 0) {
                seg_bank = active_bank_;
                seg_sx   = scroll_x_;
                seg_sy   = scroll_y_;
                col_end  = 320;
            } else {
                while (seg + 1 < nseg
                    && seg_first_col(line_segments_[seg + 1].hpos, true) <= col)
                    ++seg;
                const LineSegment& sg = line_segments_[seg];
                seg_bank = sg.active_bank;
                seg_sx   = sg.scroll_x;
                seg_sy   = sg.scroll_y;
                col_end  = (seg + 1 < nseg)
                         ? seg_first_col(line_segments_[seg + 1].hpos, true)
                         : 320;
                if (col_end > 320) col_end = 320;
            }

            uint8_t src_y = static_cast<uint8_t>(row + seg_sy);

            for (; col < col_end; ++col) {
                // Clip X on DESTINATION column (pre-scroll), per VHDL
                // layer2.vhd:167. Compare column byte-index directly
                // against the doubled clip register (preserved from the
                // pre-G104 semantics); do NOT divide.
                if (col < clip_x1_eff || col > clip_x2_eff)
                    continue;

                int src_col_pre = col + (seg_sx & 0x1FF);
                int src_col = (src_col_pre >= 320) ? (src_col_pre - 320) : src_col_pre;

                uint32_t l2_addr = static_cast<uint32_t>(src_col) * 256 + src_y;
                uint32_t ram_addr = compute_ram_addr(seg_bank, l2_addr, rom_in_sram);
                uint8_t byte = ram.read(ram_addr);

                // High nibble = left pixel.
                uint8_t left_nib = (byte >> 4) & 0x0F;
                uint8_t left_idx = static_cast<uint8_t>((palette_offset_ << 4) | left_nib);
                if (palette.layer2_rgb8(palette_bank_second, left_idx) != transp_rgb) {
                    dst[col * 2] = palette.layer2_colour(palette_bank_second, left_idx);
                    // VHDL zxnext.vhd:7050 — per-pixel L2 priority bit.
                    if (priority_dst)
                        priority_dst[col * 2] = palette.layer2_priority_high(palette_bank_second, left_idx);
                }

                // Low nibble = right pixel.
                uint8_t right_nib = byte & 0x0F;
                uint8_t right_idx = static_cast<uint8_t>((palette_offset_ << 4) | right_nib);
                if (palette.layer2_rgb8(palette_bank_second, right_idx) != transp_rgb) {
                    dst[col * 2 + 1] = palette.layer2_colour(palette_bank_second, right_idx);
                    if (priority_dst)
                        priority_dst[col * 2 + 1] = palette.layer2_priority_high(palette_bank_second, right_idx);
                }
            }
        }
    }
}

// GH #27 S4 — the ONE field list (design §9.2). Block 7 of the byte-identity
// stream (§17.1), 12 bytes. Declaration order IS the stream order.
//
// NOT DECLARED: the per-scanline scroll / clip / bank / enable / NR 0x70
// change-logs and their baselines — §9.5(8), rebuilt every frame by
// `start_frame()`, which `load_state` calls after the walk (GH #261).
//
// `resolution_` is NR 0x70 bits 5:4 and is a plain `u8` rather than an
// `enum8`: it is not a C++ enum in this class, and three of its four
// ordinals are meaningful (256x192x8, 320x256x8, 640x256x4) with the fourth
// reserved, so there is no closed name set the VHDL sanctions for it.
//
// No field carries a DECLARED DEFAULT: §12.2's gate for them is S6's.
void Layer2::describe_state(jnext::save::StateDesc& d)
{
    d.u8("active_bank", active_bank_);
    d.u8("shadow_bank", shadow_bank_);
    d.u16("scroll_x", scroll_x_);
    d.u8("scroll_y", scroll_y_);
    d.u8("palette_offset", palette_offset_);
    d.u8("resolution", resolution_);
    d.boolean("enabled", enabled_);
    d.u8("clip_x1", clip_x1_); d.u8("clip_x2", clip_x2_);
    d.u8("clip_y1", clip_y1_); d.u8("clip_y2", clip_y2_);
}

void Layer2::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Layer2::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);

    // GH #261 — re-baseline the scroll/clip/bank/enable/NR 0x70 logs from
    // the state just loaded, so a render before the next begin_new_frame()
    // (Emulator::rewind_to_frame) cannot rewind the live registers to the
    // pre-restore frame's baseline. See PaletteManager::load_state.
    start_frame();
}
