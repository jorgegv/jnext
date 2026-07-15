#include "video/renderer.h"
#include "core/log.h"
#include "core/saveable.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "video/palette.h"
#include "video/layer2.h"
#include "video/sprites.h"
#include "video/tilemap.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Compositor per-pixel trace (Task 1 Phase A — parallax.nex investigation).
// Configured via Renderer::set_compositor_trace, driven by the
// `--compositor-trace FILE [--compositor-trace-frame N]` CLI flags.
// One CSV row per visible pixel of the target frame; the file is opened
// lazily on first call to render_frame and closed after that frame is
// done. Zero cost when no path is set (trace_active_ stays false; the
// hot inner loop in composite_scanline skips the dump branch).
// ---------------------------------------------------------------------------

void Renderer::set_compositor_trace(const std::string& path, int target_frame) {
    trace_path_         = path;
    trace_target_frame_ = target_frame;
    trace_frame_counter_ = 0;
    if (trace_fp_) {
        std::fclose(trace_fp_);
        trace_fp_ = nullptr;
    }
    trace_active_ = false;
}

// ---------------------------------------------------------------------------
// Host-side layer mask (--delayed-screenshot-layers, Task 22b)
// ---------------------------------------------------------------------------

namespace {

struct LayerName {
    const char* name;
    uint8_t     bits;
};

// The complete, authoritative list of accepted names. Lowercase only —
// the CLI contract is "names exactly ula, layer2, sprites, tiles, all".
constexpr LayerName kLayerNames[] = {
    {"ula",     Renderer::LAYER_ULA},
    {"layer2",  Renderer::LAYER_LAYER2},
    {"sprites", Renderer::LAYER_SPRITES},
    {"tiles",   Renderer::LAYER_TILES},
    {"all",     Renderer::LAYER_ALL},
};

const char* kValidNames = "ula, layer2, sprites, tiles, all";

} // namespace

bool Renderer::parse_layer_mask(const std::string& spec, uint8_t& mask,
                                std::string& error)
{
    error.clear();
    if (spec.empty()) {
        error = std::string("empty layer list (valid names: ") + kValidNames + ")";
        return false;
    }

    uint8_t acc = 0;
    size_t  pos = 0;
    while (pos <= spec.size()) {
        const size_t comma = spec.find(',', pos);
        const std::string tok =
            spec.substr(pos, (comma == std::string::npos) ? std::string::npos
                                                          : comma - pos);
        if (tok.empty()) {
            error = std::string("empty layer name in '") + spec
                  + "' (valid names: " + kValidNames + ")";
            return false;
        }

        uint8_t bits = 0;
        for (const auto& ln : kLayerNames) {
            if (tok == ln.name) { bits = ln.bits; break; }
        }
        if (bits == 0) {
            error = "unknown layer name '" + tok
                  + "' (valid names: " + kValidNames + ")";
            return false;
        }
        // Any overlap means the user named the same layer twice — either
        // literally (ula,ula) or via 'all' (all,ula). Say so; never fold
        // it away silently.
        if (acc & bits) {
            error = "layer '" + tok + "' selected twice in '" + spec + "'";
            return false;
        }
        acc |= bits;

        if (comma == std::string::npos) break;
        pos = comma + 1;
        // A trailing comma leaves pos == size(): the next iteration reads an
        // empty token and errors out, which is what we want.
    }

    mask = acc;
    return true;
}

std::string Renderer::layer_mask_to_string(uint8_t mask)
{
    mask &= LAYER_ALL;
    if (mask == LAYER_ALL) return "all";
    if (mask == 0)         return "none";

    std::string out;
    for (const auto& ln : kLayerNames) {
        if (ln.bits == LAYER_ALL) continue;  // composite name, not a layer
        if (mask & ln.bits) {
            if (!out.empty()) out += ',';
            out += ln.name;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// render_frame — per-scanline compositing
// ---------------------------------------------------------------------------
//
// For each scanline:
//   1. Render ULA (border + display) into ula_line_
//   2. Render Layer 2 into layer2_line_ (transparent where disabled/no pixel)
//   3. Render Tilemap into tilemap_line_ (transparent where disabled/no pixel)
//   4. Render Sprites into sprite_line_ (transparent where no sprite)
//   5. Composite all layers using NextREG 0x15 priority order
//
// VHDL reference: zxnext.vhd lines 7193-7354.

void Renderer::render_frame(uint32_t* framebuffer, Mmu& mmu, Ram& ram,
                             PaletteManager& palette,
                             Layer2& layer2,
                             SpriteEngine* sprites,
                             Tilemap* tilemap)
{
    // Compositor trace (CLI-gated, see set_compositor_trace + header).
    // First frame after the path is set opens the file; the frame-counter
    // races up to trace_target_frame_, then the per-pixel dump fires for
    // exactly that frame and the file is closed below.
    if (!trace_path_.empty() && !trace_fp_ && trace_frame_counter_ == 0) {
        trace_fp_ = std::fopen(trace_path_.c_str(), "w");
        if (trace_fp_) {
            std::fprintf(trace_fp_,
                "frame,row,x,ula_px,l2_px,spr_px,tm_px,"
                "ula_transp,l2_transp,spr_transp,tm_transp,"
                "l2_prio,ula_border,tm_below,tm_textmode,"
                "result_px\n");
        }
    }
    const int trace_frame_no = ++trace_frame_counter_;
    trace_active_ = (trace_fp_ && trace_frame_no == trace_target_frame_);

    // Per-scanline palette: rewind to the frame's baseline so the
    // line-by-line apply below sees the same starting state the Z80
    // saw at frame start. The change-log was populated during emulation
    // (see Emulator::on_scanline → palette_.set_current_line). Without
    // this, the live palette holds end-of-frame state and every
    // scanline renders identically (e.g. beast.nex sky gradient
    // collapses to its last colour). TASK-PER-SCANLINE-PALETTE-PLAN.md.
    //
    // Note: in replay_mode_ Emulator::run_frame skips render_frame, so
    // this rewind also doesn't fire. That is intentional — the next
    // live frame's start_frame() snapshots a fresh baseline, and the
    // live palette state has not been disturbed by the skipped render.
    palette.rewind_to_baseline();

    // Per-scanline Layer 2 scroll: same rewind+replay pattern as the
    // palette path above. Required for beast.nex bottom-band parallax
    // (Copper writes NR 0x16 at successive raster targets with
    // progressively higher values).
    layer2.rewind_to_baseline();

    // Per-scanline sprite attributes: same rewind+replay pattern.
    // Required for parallax.nex (Z80N DMA bulk-streams sprite attributes
    // via port 0x57 at ~140 DMA-LOAD per second to multiplex sprites
    // across multiple Y positions per frame). VHDL sprites.vhd:327-470.
    if (sprites) {
        sprites->rewind_to_baseline();
    }

    // Per-scanline port-0xFF Timex screen-mode (G07).
    ula_.rewind_to_baseline();
    // Per-scanline ULA scroll (G08).
    ula_.rewind_scroll_to_baseline();
    // Per-scanline ULA active-palette selector (G10).
    ula_.palsel_rewind_to_baseline();
    // Per-scanline tilemap NR 0x6B (G06: b0/b1/b3/b6/b7).
    if (tilemap) {
        tilemap->rewind_nr6b_to_baseline();
    }
    // G12 — Nirvana-class attribute-mux (Category B, per-scanline
    // replay of mid-frame attribute writes). Always-on — see
    // Mmu::attr_mux_start_frame() / Ula::attr_vram_read().
    mmu.attr_mux_rewind_to_baseline();

    for (int row = 0; row < FB_HEIGHT; ++row) {
        // Replay log entries tagged with this scanline before any
        // layer rendering reads palette state.
        palette.apply_changes_for_line(row);
        layer2.apply_changes_for_line(row);
        if (sprites) {
            sprites->apply_changes_for_line(row);
        }
        // Replay port-0xFF (Timex screen-mode) writes (G07).
        ula_.apply_changes_for_line(row);
        // Apply ULA scroll log for this scanline (G08).
        ula_.apply_scroll_changes_for_line(row);
        // Apply ULA active-palette selector log (G10).
        ula_.palsel_apply_changes_for_line(row);
        // Apply tilemap NR 0x6B log (G06: b0/b1/b3/b6/b7).
        if (tilemap) {
            tilemap->apply_nr6b_changes_for_line(row);
        }
        // Apply attribute-mux log for this scanline (G12).
        mmu.attr_mux_apply_line(row);

        render_row(framebuffer + row * FB_WIDTH, row, mmu, ram, palette,
                   layer2, sprites, tilemap);
    }

    // Close the trace file once the target frame is done so subsequent
    // frames don't keep appending (one-shot per --compositor-trace flag).
    if (trace_active_ && trace_fp_) {
        std::fclose(trace_fp_);
        trace_fp_ = nullptr;
        trace_active_ = false;
    }

    // Drain log entries that landed in vblank (line >= FB_HEIGHT) for
    // every per-scanline log: rewind_to_baseline() at the top of this
    // function undoes the direct live mutation done by their NR/port
    // writers, and apply_*_changes_for_line(0..H-1) only replays entries
    // tagged at visible scanlines. Without these flushes, vblank writes
    // are lost forever — the next frame's start_frame() snapshots a
    // baseline missing them. Surfaced by tilemap_demo at NR 0x07 >= 0x02
    // (CPU 14/28 MHz): setup completes during vblank, so the entire
    // tilemap palette stays at the zero baseline and every tilemap pixel
    // renders as palette[0] = black.
    palette.flush_remaining_changes();
    layer2.flush_remaining_changes();
    if (sprites) {
        sprites->flush_remaining_changes();
    }
    ula_.flush_remaining_changes();
    ula_.flush_remaining_scroll_changes();
    ula_.palsel_flush_remaining_changes();
    if (tilemap) {
        tilemap->flush_remaining_nr6b_changes();
    }
    mmu.attr_mux_flush_remaining();

    // Advance ULA flash state once per frame.
    ula_.advance_flash();
}

// ---------------------------------------------------------------------------
// run_sprite_side_effects — sprites-only pass for render-skipped frames
// ---------------------------------------------------------------------------
//
// See the header comment: sprite collision (port 0x303B bit 0) and per-line
// budget overtime (bit 1) — VHDL sprites.vhd:971-995 — are computed inside
// SpriteEngine::render_scanline, so a frame skipped by the C6 frontend hint
// must still run the sprite pipeline or software polling 0x303B misses real
// events. Mirrors render_frame's sprite handling exactly: the same
// rewind/apply/flush change-log lifecycle, the same per-row sprites_visible()
// gate, the same render_scanline call. Pixels land in sprite_line_ and are
// discarded (render_row refills it to TRANSPARENT before every use, so no
// stale pixel leaks into the next rendered frame).

void Renderer::run_sprite_side_effects(SpriteEngine* sprites,
                                       PaletteManager& palette)
{
    if (!sprites)
        return;

    sprites->rewind_to_baseline();
    for (int row = 0; row < FB_HEIGHT; ++row) {
        sprites->apply_changes_for_line(row);
        if (sprites->sprites_visible()) {
            sprites->render_scanline(sprite_line_.data(), row, palette);
        }
    }
    sprites->flush_remaining_changes();
}

// ---------------------------------------------------------------------------
// render_row — render every layer for one row and composite it
// ---------------------------------------------------------------------------
//
// Lifted verbatim out of render_frame's row loop (Task 36, no behavioural
// change) so the debugger's "All layers" view composites through the SAME
// code the live output does, instead of a second, drifting compositor.
//
// The caller owns the per-scanline change-log replay (rewind → apply row →
// … → flush): render_frame does it inline above, and the debugger panel does
// it with the identical helper sequence.  This function only reads the state
// those logs leave live; it never advances a cursor and never touches the
// once-per-frame ULA flash counter.

void Renderer::render_row(uint32_t* out, int row, Mmu& mmu, Ram& ram,
                          PaletteManager& palette, Layer2& layer2,
                          SpriteEngine* sprites, Tilemap* tilemap)
{
    const int screen_row = row - DISP_Y;  // display row (negative = top border)

    // --- Clear layer buffers to transparent ---
    // Clear the FULL 640-wide line buffer: layers still emit only into
    // [0..319] at Phase 1; the upper half stays TRANSPARENT until the
    // doubling scaffold below overwrites it.
    std::fill_n(layer2_line_.begin(), FB_WIDTH, TRANSPARENT);
    std::fill_n(sprite_line_.begin(), FB_WIDTH, TRANSPARENT);
    std::fill_n(tilemap_line_.begin(), FB_WIDTH, TRANSPARENT);
    std::fill_n(tm_pixel_below_.begin(), FB_WIDTH, false);
    std::fill_n(tm_pixel_textmode_.begin(), FB_WIDTH, false);
    std::fill_n(layer2_priority_.begin(), FB_WIDTH, false);
    std::fill_n(ula_border_.begin(), FB_WIDTH, false);

    // --- Render each layer for this scanline ---
    const bool in_display = (screen_row >= 0 && screen_row < DISP_H);

    // Layer 2 — 256×192 is active only in the display area;
    // 320×256 and 640×256 ("wide") modes cover all 256 rows.
    if (layer2.enabled()) {
        if (layer2.is_wide() || in_display) {
            // Phase 3 (G104): Layer 2 emits 640 natively (resolution 0
            // pixel-doubled into [DISP_X..DISP_X+512), resolution 1
            // pixel-doubled across the full 640, resolution 2/3 native).
            // G179 issue #3: per-pixel priority bit (NR 0x44 b7) flows
            // from the L2 palette into layer2_priority_[] so the
            // compositor can promote L2 above sprites for opaque pixels
            // whose palette entry has the priority bit set (VHDL
            // zxnext.vhd:7050, 7220). The buffer is zero-filled at the
            // top of this function so transparent pixels stay false.
            // transparent_rgb_for_line(row), NOT palette.global_transparency()
            // (the live NR 0x14 member): VHDL zxnext.vhd:1137,5226,6822,
            // 6912-6913,7078 pipeline NR 0x14 through the SAME stage0/1a/1/2
            // register chain composite_scanline reads via nr14_rgb (Task 45),
            // so a Copper MOVE to NR 0x14 mid-frame must not affect the row
            // it lands on. Layer2::render_scanline SKIP-WRITES transparent
            // pixels, so before Task 46 a wrongly-transparent pixel here was
            // destroyed before composite_scanline's own NR 0x14 check ever
            // saw it — unrecoverable.
            layer2.render_scanline(layer2_line_.data(), row, ram, palette,
                                   transparent_rgb_for_line(row),
                                   mmu.rom_in_sram(),
                                   layer2_priority_.data());
        }
    }

    // Tilemap — covers the full 640-wide framebuffer (VHDL: vcounter(8)='0').
    // G104 phase 4: native 640 emit (80-col 1:1, 40-col internal double).
    if (tilemap && tilemap->enabled()) {
        tilemap->render_scanline(tilemap_line_.data(),
                                 tm_pixel_below_.data(),
                                 row, ram, palette,
                                 tm_pixel_textmode_.data());
    }

    // Sprites — Y coordinates are in absolute framebuffer space (0-255)
    if (sprites && sprites->sprites_visible()) {
        sprites->render_scanline(sprite_line_.data(), row, palette);
    }

    // Render ULA scanline (G104 Phase 2: native 640 emit). Pass
    // ula_border_ so the ULA renderer marks every cell painted with
    // the border colour for the compositor stage's `ula_border_2`
    // mux gate (VHDL zxnext.vhd:7256/7266/7278; source signal at
    // zxula.vhd:415, exposed as o_ula_border at :567). The pre-fill
    // above leaves display-area cells at false, so the ULA only needs
    // to write the border strips (left/right per display row + entire
    // top/bottom border rows).
    const uint32_t fb_argb = rrrgggbb_to_argb(fallback_per_line_[row]);
    ula_.render_scanline(ula_line_.data(), row, mmu, ula_border_.data());
    // When ULA is disabled (NR 0x68 bit 7 = 1), the whole ULA output
    // is transparent — display AND border — per VHDL zxnext.vhd:7103
    //   ula_transparent <= '1' when (ula_mix_transparent = '1')
    //                             or (ula_en_2 = '0') else '0';
    // That makes the border pixels fall through to the fallback
    // colour (NR 0x4A) just like the display area, which is what
    // copper_demo relies on to paint the rainbow across the border.
    //
    // Read the per-line snapshot (not the live Ula::ula_enabled())
    // so a Copper MOVE to NR 0x68 mid-frame flips transparency only
    // for rows that follow the toggle. Matches the per-line fallback
    // snapshot handling already used for NR 0x4A.
    if (!ula_enabled_per_line_[row]) {
        // G104 Phase 2: ULA emits native 640, zero the full line.
        std::fill_n(ula_line_.begin(), FB_WIDTH, TRANSPARENT);
    }

    // ULA clip window (NextREG 0x1A) — see Renderer::apply_ula_clip.
    apply_ula_clip(ula_line_.data(), row);

    trace_current_row_ = row;
    composite_scanline(out, fb_argb, row);
}

// ---------------------------------------------------------------------------
// apply_ula_clip — mask one ULA scanline with the NR 0x1A clip window
// ---------------------------------------------------------------------------
//
// Lifted verbatim out of render_frame (no behavioural change) so the debugger's
// ULA views can apply the same mask. Layer 2 / Tilemap / Sprites each clip
// inside their own render_scanline; the ULA does not, because in VHDL the clip
// is a compositor-stage signal (zxnext.vhd:7104 — `ula_clipped` is OR'd into
// `ula_transparent`), so Ula::render_scanline emits an unclipped line.
//
// G104 Phase 2: the clip math runs on the canonical 640 grid (DISP_X=64,
// DISP_W=512, FB_WIDTH=640) since the ULA now emits native 640.

void Renderer::apply_ula_clip(uint32_t* line, int row) const
{
    // Read the per-scanline NR 0x1A snapshot, NOT the live Ula::clip_*()
    // getters: the VHDL clip comparators consume the registers per pixel
    // (zxnext.vhd:988-991; NR 0x1A rotating write + y2 clamp at 6779-6783),
    // so a mid-frame write must affect only rows the beam has not yet
    // passed. The live getters here would retroactively re-mask every row
    // rendered after the write (the Task 43/45/46 per-line-deferral bug
    // class). Out-of-range rows fall back to the live values.
    const UlaClipWindow cw = ula_clip_for_line(row);
    const uint8_t cx1 = cw.x1;
    const uint8_t cx2 = cw.x2;
    const uint8_t cy1 = cw.y1;
    const uint8_t cy2 = cw.y2;

    const int  screen_row     = row - DISP_Y;
    const bool row_in_display = (screen_row >= 0 && screen_row < DISP_H);
    const int  vc             = screen_row;

    const bool y_clipped   = row_in_display && (vc < cy1 || vc > cy2);
    const bool in_border_y = !row_in_display;

    if (y_clipped) {
        for (int x = DISP_X; x < DISP_X + DISP_W; ++x)
            line[x] = TRANSPARENT;
    }

    if (in_border_y || y_clipped) {
        bool clip_border_row;
        if (in_border_y) {
            clip_border_row = (row < DISP_Y) ? (cy1 > 0) : (cy2 < (DISP_H - 1) || cy1 > cy2 || cy1 >= DISP_H);
        } else {
            clip_border_row = true;
        }
        if (clip_border_row) {
            std::fill_n(line, FB_WIDTH, TRANSPARENT);
        }
    }

    if (row_in_display && !y_clipped) {
        // NR 0x1A clip-x range is in 256-pixel display-coordinate space
        // (zxnext.vhd:6779-6783); under G104 native-512 each source clip-coord
        // covers two adjacent framebuffer cells (the same VHDL-faithful pixel
        // doubling the ULA renderer does — zxula.vhd:390-393).
        for (int x = 0; x < DISP_W; ++x) {
            const int src_x = x / 2;
            if (src_x < cx1 || src_x > cx2)
                line[DISP_X + x] = TRANSPARENT;
        }
        const bool left_clipped  = (cx1 > 0);
        const bool right_clipped = (cx2 < 255) || (cx1 > cx2);
        if (left_clipped) {
            for (int x = 0; x < DISP_X; ++x)
                line[x] = TRANSPARENT;
        }
        if (right_clipped) {
            for (int x = DISP_X + DISP_W; x < FB_WIDTH; ++x)
                line[x] = TRANSPARENT;
        }
    }
}

// ---------------------------------------------------------------------------
// composite_scanline — combine layers per NextREG 0x15 priority
// ---------------------------------------------------------------------------
//
// VHDL compositor (zxnext.vhd ~7090-7354):
//   Stage 2 determines transparency per layer, merges ULA+Tilemap, then
//   applies the 3-bit layer_priority mode to pick the output pixel.
//
//   Transparency detection (VHDL 7100-7123):
//     ULA:    palette RGB[8:1] == NR 0x14 OR ula_clipped OR ula_en=0
//     Layer2: palette RGB[8:1] == NR 0x14 OR pixel_en=0
//     Sprite: pixel_en=0 only (no RGB compare)
//     TM:     pixel_en=0 OR (text_mode AND palette RGB==NR 0x14) OR tm_en=0
//
//   ULA/TM merge (VHDL 7115-7116):
//     ulatm_transparent = ula_transparent AND tm_transparent
//     ulatm_rgb = TM when TM opaque AND (TM above OR ULA transparent),
//                 else ULA
//
//   Modes 6/7 blend (VHDL 7286-7354):
//     mix_rgb/mix_top/mix_bot from ULA blend mode (NR 0x68 bits 6:5,
//     VHDL 7141-7178):
//       "00" default: mix_rgb=ULA, mix_top=TM above, mix_bot=TM below.
//       "10":         mix_rgb=ula_final (post-stencil), top/bot transp.
//       "11":         mix_rgb=TM, mix_top=ULA above, mix_bot=ULA below.
//       "01"/others:  mix_rgb transp; top/bot swap TM/ULA by tm_pixel_below.
//     Per-channel arithmetic: L2 + mix_rgb
//       Mode 110: additive with clamp to max
//       Mode 111: subtractive (gated on mix_rgb not transparent)
//     Output chain: L2_priority → mixer, mix_top, sprite, mix_bot, L2 → mixer

// Channel extraction from ARGB (reverses rrrgggbb_to_argb).
static uint8_t argb_r3(uint32_t argb) { return (argb >> 21) & 7; }
static uint8_t argb_g3(uint32_t argb) { return (argb >> 13) & 7; }
static uint8_t argb_b2(uint32_t argb) { return (argb >>  6) & 3; }

// Reconstruct ARGB from 3/3/2 channel values.
static uint32_t channels_to_argb(uint8_t r3, uint8_t g3, uint8_t b2) {
    uint8_t rgb8 = static_cast<uint8_t>((r3 << 5) | (g3 << 2) | b2);
    return Renderer::rrrgggbb_to_argb(rgb8);
}

// composite_scanline — dispatch once per scanline on the (per-line constant)
// NR 0x15 layer_priority_. Task 27 C8: the priority mode cannot change within a
// scanline — nothing in composite_scanline_mode's inner loop mutates
// layer_priority_, and the emulator commits NR 0x15 (renderer_.set_layer_priority,
// emulator.cpp) before render_frame runs. Selecting the specialised loop here
// hoists the former per-pixel `switch (layer_priority_)` (163,840×/frame) out of
// the inner loop: PRIO is a compile-time constant inside the specialisation, so
// the switch folds to the single taken arm with no per-pixel branch. Output is
// byte-identical to the pre-hoist single-loop form.
void Renderer::composite_scanline(uint32_t* dst, uint32_t fallback_argb, int row)
{
    switch (layer_priority_) {
        case 0: composite_scanline_mode<0>(dst, fallback_argb, row); break;
        case 1: composite_scanline_mode<1>(dst, fallback_argb, row); break;
        case 2: composite_scanline_mode<2>(dst, fallback_argb, row); break;
        case 3: composite_scanline_mode<3>(dst, fallback_argb, row); break;
        case 4: composite_scanline_mode<4>(dst, fallback_argb, row); break;
        case 5: composite_scanline_mode<5>(dst, fallback_argb, row); break;
        case 6: composite_scanline_mode<6>(dst, fallback_argb, row); break;
        case 7: composite_scanline_mode<7>(dst, fallback_argb, row); break;
    }
}

template<int PRIO>
void Renderer::composite_scanline_mode(uint32_t* dst, uint32_t fallback_argb, int row)
{
    // Pre-compute NR 0x14 transparency reference (RGB portion only).
    // VHDL 7100: ula_rgb_2(8 downto 1) = transparent_rgb_2
    //
    // Read via transparent_rgb_for_line(row), not the live transparent_rgb_
    // member: VHDL zxnext.vhd:1137,5226,6822,6912-6913,7078 pipeline NR 0x14
    // (nr_14_global_transparent_rgb -> transparent_rgb_0 -> transparent_rgb_1a
    // -> transparent_rgb_1 -> transparent_rgb_2) through the exact same
    // stage0/1a/1/2 register chain as ula_en, so a Copper MOVE that changes
    // NR 0x14 mid-frame must not affect the row it lands on (Task 45).
    const uint32_t nr14_rgb = rrrgggbb_to_argb(transparent_rgb_for_line(row)) & 0x00FFFFFF;

    // Host-side layer mask (--delayed-screenshot-layers). A masked-out
    // layer is forced transparent at the compositor input, i.e. treated
    // exactly as if its hardware enable bit were clear (see LayerMask in
    // renderer.h). Default LAYER_ALL leaves every flag below untouched.
    const bool mask_ula     = (layer_mask_ & LAYER_ULA)     == 0;
    const bool mask_l2      = (layer_mask_ & LAYER_LAYER2)  == 0;
    const bool mask_sprites = (layer_mask_ & LAYER_SPRITES) == 0;
    const bool mask_tiles   = (layer_mask_ & LAYER_TILES)   == 0;
    // Stencil mode requires ALL THREE enables (VHDL zxnext.vhd:7130):
    //   if ula_stencil_mode_2 = '1' and ula_en_2 = '1' and tm_en_2 = '1'
    // so masking out the ULA, the tilemap, OR disabling the ULA itself
    // (NR 0x68 bit 7, per-line snapshot ula_enabled_per_line_) must take
    // the stencil AND-branch down with it and fall through to the
    // ordinary ulatm merge (VHDL 7134-7135), which — with ula_transparent
    // forced by ula_en_2='0' (VHDL 7103) — degrades to "show the tile
    // pixel". Getting this half-right is a real bug in both directions:
    // stencil_rgb is transparent whenever either input is (7112), so
    // leaving the AND-branch selected with one input disabled erases the
    // surviving layer (falls to the NR 0x4A fallback colour) instead of
    // showing it.
    //
    // ula_stencil_mode_2 is read via stencil_mode_for_line(row), not the
    // live stencil_mode_ member: VHDL zxnext.vhd:5445/6810/6897-6898/7064
    // pipelines NR 0x68 bit 0 through the same stage0/1a/1/2 register
    // chain as ula_en, so a Copper MOVE that flips the bit mid-frame must
    // not affect the row it lands on (Task 43).
    const bool stencil_active =
        stencil_mode_for_line(row) && tm_enabled_ && !mask_tiles && !mask_ula &&
        ula_enabled_per_line_[row];

    for (int x = 0; x < FB_WIDTH; ++x) {
        const uint32_t ula_px  = ula_line_[x];
        const uint32_t l2_px   = layer2_line_[x];
        const uint32_t spr_px  = sprite_line_[x];
        const uint32_t tm_px   = tilemap_line_[x];

        // --- Transparency detection (VHDL 7100-7123) ---
        // The `mask_*` terms are the host-side layer mask; they sit
        // alongside the hardware enable bits because that is precisely
        // what they emulate (a masked layer == a disabled layer).
        const bool ula_transp = mask_ula || is_transparent(ula_px) ||
                                ((ula_px & 0x00FFFFFF) == nr14_rgb);
        // Task 46: `(l2_px & 0x00FFFFFF) == nr14_rgb` is now PROVABLY
        // UNREACHABLE whenever `is_transparent(l2_px)` is false. Unlike
        // the ULA (which never evaluates NR 0x14 itself — VHDL 7100 does
        // it once, here, at the compositor), Layer2::render_scanline
        // already skip-writes any pixel whose palette RRRGGGBB equals
        // `transparent_rgb_for_line(row)` (VHDL zxnext.vhd:7121), using
        // the IDENTICAL snapshot `nr14_rgb` is built from two lines above.
        // rrrgggbb_to_argb()'s R/G expansion is the same injective 3-bit
        // function `layer2_colour()`'s full-precision RGB333->ARGB uses
        // for R/G, so an R/G mismatch in one representation is an R/G
        // mismatch in the other. For B, rrrgggbb_to_argb() only carries 2
        // bits (register format) while layer2_colour() carries the full 3
        // (palette format); the 8-bit values each side's expansion can
        // produce (`{0,36,73,109,146,182,219,255}` for 3-bit vs.
        // `{0,85,170,255}` for 2-bit-then-doubled) intersect ONLY at the
        // endpoints 0 and 255 — precisely the b3 values whose top 2 bits
        // (`b3>>1`) already equal the compared 2-bit value, i.e. exactly
        // the cases `layer2_rgb8() == transparent_rgb` already covers.
        // So a WRITTEN Layer 2 pixel (opaque per Layer2's own gate against
        // this SAME snapshot) can never coincide with `nr14_rgb` here.
        // Before Task 46, Layer2 read a DIFFERENT (live) value than this
        // `nr14_rgb`, so the two gates could legitimately disagree — this
        // clause was the (broken) recovery path for the "wrongly opaque"
        // direction, but could never recover the "wrongly transparent /
        // skip-written" direction (the pixel was already destroyed). Kept
        // rather than deleted: it is harmless, documents the equivalence,
        // and a future change to either expansion function would silently
        // resurrect a real bug if this clause were gone.
        const bool l2_transp  = mask_l2 || is_transparent(l2_px) ||
                                ((l2_px & 0x00FFFFFF) == nr14_rgb);
        // VHDL 6934/7118: sprite_en=0 forces all sprites transparent.
        const bool spr_transp = mask_sprites || is_transparent(spr_px) || !sprite_en_;
        // VHDL zxnext.vhd:7109 — `tm_transparent <= '1' when (tm_pixel_en_2 = '0')
        // or (tm_pixel_textmode_2 = '1' and tm_rgb_2(8 downto 1) =
        // transparent_rgb_2) or (tm_en_2 = '0')`.  is_transparent(tm_px)
        // covers both tm_pixel_en_2='0' (suppressed by render_scanline) and
        // tm_en_2='0' (Tilemap::render_scanline early-returns when disabled,
        // leaving the line buffer at TRANSPARENT).  The per-pixel textmode
        // flag gates the RGB match clause (G98 + G101).
        const bool tm_transp  = mask_tiles || is_transparent(tm_px) ||
                                (tm_pixel_textmode_[x] &&
                                 (tm_px & 0x00FFFFFF) == nr14_rgb);

        // L2 priority promotion flag (VHDL 7220 etc.: palette bit 15).
        const bool l2_prio = !l2_transp && layer2_priority_[x];
        // Border exception flag (VHDL 7256/7266/7278: ula_border_2).
        const bool ula_border = ula_border_[x];

        // --- ULA/TM merge (VHDL 7112-7116) ---
        uint32_t u_px;
        bool ulatm_transp;
        if (stencil_active) {
            // Stencil mode (VHDL 7112-7113): bitwise AND of ULA and TM RGB.
            // stencil_transparent = ula_transp OR tm_transp.
            if (ula_transp || tm_transp) {
                ulatm_transp = true;
                u_px = TRANSPARENT;
            } else {
                ulatm_transp = false;
                // Per-channel AND in 3/3/2 bit space
                uint8_t r = argb_r3(ula_px) & argb_r3(tm_px);
                uint8_t g = argb_g3(ula_px) & argb_g3(tm_px);
                uint8_t b = argb_b2(ula_px) & argb_b2(tm_px);
                u_px = channels_to_argb(r, g, b);
            }
        } else {
            // Normal ULA/TM merge (VHDL 7115-7116)
            ulatm_transp = ula_transp && tm_transp;
            if (!tm_transp && (!tm_pixel_below_[x] || ula_transp)) {
                u_px = tm_px;
            } else {
                u_px = ula_transp ? TRANSPARENT : ula_px;
            }
        }

        uint32_t result = fallback_argb;

        switch (PRIO) {
            case 0:  // SLU (VHDL 7218)
                if (l2_prio)                result = l2_px;   // VHDL 7220
                else if (!spr_transp)       result = spr_px;
                else if (!l2_transp)        result = l2_px;
                else if (!ulatm_transp)     result = u_px;
                break;

            case 1:  // LSU (VHDL 7230)
                if (l2_prio)                result = l2_px;   // VHDL 7232 (no-op: L2 already top)
                else if (!l2_transp)        result = l2_px;
                else if (!spr_transp)       result = spr_px;
                else if (!ulatm_transp)     result = u_px;
                break;

            case 2:  // SUL (VHDL 7240)
                if (l2_prio)                result = l2_px;   // VHDL 7242
                else if (!spr_transp)       result = spr_px;
                else if (!ulatm_transp)     result = u_px;
                else if (!l2_transp)        result = l2_px;
                break;

            case 3: {  // LUS (VHDL 7252)
                // Border exception (VHDL 7256): suppress U when border AND
                // tm transparent AND sprite opaque (all three required).
                const bool border_exc = ula_border && tm_transp && !spr_transp;
                const bool u_eff = !ulatm_transp && !border_exc;
                if (l2_prio)                result = l2_px;   // VHDL 7254
                else if (!l2_transp)        result = l2_px;
                else if (u_eff)             result = u_px;
                else if (!spr_transp)       result = spr_px;
                break;
            }

            case 4: {  // USL (VHDL 7262)
                // Border exception (VHDL 7266): all three conditions required.
                const bool border_exc = ula_border && tm_transp && !spr_transp;
                const bool u_eff = !ulatm_transp && !border_exc;
                if (l2_prio)                result = l2_px;   // VHDL 7264
                else if (u_eff)             result = u_px;
                else if (!spr_transp)       result = spr_px;
                else if (!l2_transp)        result = l2_px;
                break;
            }

            case 5: {  // ULS (VHDL 7274)
                // Border exception (VHDL 7278): all three conditions required.
                const bool border_exc = ula_border && tm_transp && !spr_transp;
                const bool u_eff = !ulatm_transp && !border_exc;
                if (l2_prio)                result = l2_px;   // VHDL 7276
                else if (u_eff)             result = u_px;
                else if (!l2_transp)        result = l2_px;
                else if (!spr_transp)       result = spr_px;
                break;
            }

            case 6:
            case 7: {
                // ULA blend mode 4-variant source selection (VHDL 7141-7178).
                const bool tm_below = tm_pixel_below_[x];

                uint32_t mix_rgb_px   = 0;
                bool     mix_rgb_transp = true;
                uint32_t mix_top_px   = 0;
                bool     mix_top_transp = true;
                uint32_t mix_bot_px   = 0;
                bool     mix_bot_transp = true;

                // ula_blend_mode_2 is read via blend_mode_for_line(row), not
                // the live blend_mode_ member: VHDL zxnext.vhd:5446/6811/
                // 6900-6901/7065 pipelines NR 0x68 bits 6:5 through the same
                // stage0/1a/1/2 register chain as ula_en, so a mid-frame
                // Copper MOVE must not affect the row it lands on (Task 43).
                switch (blend_mode_for_line(row)) {
                    case 0:  // "00" — VHDL 7142-7148 (existing default behaviour).
                        mix_rgb_px     = ula_px;
                        mix_rgb_transp = ula_transp;
                        mix_top_px     = tm_px;
                        mix_top_transp = tm_transp || tm_below;
                        mix_bot_px     = tm_px;
                        mix_bot_transp = tm_transp || !tm_below;
                        break;
                    case 2:  // "10" — VHDL 7149-7155: mix_rgb = ula_final; top/bot forced transp.
                        mix_rgb_px     = u_px;
                        mix_rgb_transp = ulatm_transp;
                        mix_top_transp = true;
                        mix_bot_transp = true;
                        break;
                    case 3:  // "11" — VHDL 7156-7162: mix_rgb = TM; top/bot both ULA.
                        mix_rgb_px     = tm_px;
                        mix_rgb_transp = tm_transp;
                        mix_top_px     = ula_px;
                        mix_top_transp = ula_transp || !tm_below;
                        mix_bot_px     = ula_px;
                        mix_bot_transp = ula_transp || tm_below;
                        break;
                    default:  // "01" / others — VHDL 7163-7176: mix_rgb transp; swap TM/ULA.
                        mix_rgb_transp = true;
                        if (tm_below) {
                            mix_top_px     = ula_px;
                            mix_top_transp = ula_transp;
                            mix_bot_px     = tm_px;
                            mix_bot_transp = tm_transp;
                        } else {
                            mix_top_px     = tm_px;
                            mix_top_transp = tm_transp;
                            mix_bot_px     = ula_px;
                            mix_bot_transp = ula_transp;
                        }
                        break;
                }

                // Extract 3/3/2 channels (zeroed when transparent per VHDL 7101/7122).
                const uint8_t l2_r = l2_transp ? 0 : argb_r3(l2_px);
                const uint8_t l2_g = l2_transp ? 0 : argb_g3(l2_px);
                const uint8_t l2_b = l2_transp ? 0 : argb_b2(l2_px);
                const uint8_t mx_r = mix_rgb_transp ? 0 : argb_r3(mix_rgb_px);
                const uint8_t mx_g = mix_rgb_transp ? 0 : argb_g3(mix_rgb_px);
                const uint8_t mx_b = mix_rgb_transp ? 0 : argb_b2(mix_rgb_px);

                // Raw per-channel 4-bit sums (VHDL 7201-7203).
                uint8_t r_sum = l2_r + mx_r;
                uint8_t g_sum = l2_g + mx_g;
                uint8_t b_sum = l2_b + mx_b;

                uint32_t mixer_argb;
                if (PRIO == 6) {
                    // Additive with clamp (VHDL 7288-7298).
                    const uint8_t r_out = std::min<uint8_t>(r_sum, 7);
                    const uint8_t g_out = std::min<uint8_t>(g_sum, 7);
                    const uint8_t b_out = std::min<uint8_t>(b_sum, 3);
                    mixer_argb = channels_to_argb(r_out, g_out, b_out);
                } else {
                    // Subtractive gated on mix_rgb not transparent (VHDL 7314).
                    if (!mix_rgb_transp) {
                        auto sub = [](uint8_t s) -> uint8_t {
                            if (s <= 4) return 0;
                            if (((s >> 2) & 3) == 3) return 7;  // >= 12
                            return static_cast<uint8_t>((s + 0x0Bu) & 0x0Fu);
                        };
                        r_sum = sub(r_sum);
                        g_sum = sub(g_sum);
                        b_sum = sub(b_sum);
                    }
                    mixer_argb = channels_to_argb(r_sum & 7, g_sum & 7, b_sum & 3);
                }

                // Output cascade (VHDL 7300-7310 add, 7342-7352 sub).
                // Per VHDL: mix_top / mix_bot sources are per-variant, NOT
                // always TM (that was the mode-00 special case).
                if (l2_prio)                  result = mixer_argb;
                else if (!mix_top_transp)     result = mix_top_px;
                else if (!spr_transp)         result = spr_px;
                else if (!mix_bot_transp)     result = mix_bot_px;
                else if (!l2_transp)          result = mixer_argb;
                break;
            }
        }

        dst[x] = result;

        // Per-pixel trace (CLI-gated, see Renderer::set_compositor_trace).
        if (trace_active_ && trace_fp_) {
            std::fprintf(trace_fp_,
                "%d,%d,%d,"
                "0x%08x,0x%08x,0x%08x,0x%08x,"
                "%d,%d,%d,%d,%d,%d,%d,%d,"
                "0x%08x\n",
                trace_target_frame_, trace_current_row_, x,
                ula_px, l2_px, spr_px, tm_px,
                ula_transp ? 1 : 0,
                l2_transp  ? 1 : 0,
                spr_transp ? 1 : 0,
                tm_transp  ? 1 : 0,
                l2_prio    ? 1 : 0,
                ula_border ? 1 : 0,
                tm_pixel_below_[x]    ? 1 : 0,
                tm_pixel_textmode_[x] ? 1 : 0,
                result);
        }
    }
}

void Renderer::save_state(StateWriter& w) const
{
    ula_.save_state(w);
    w.write_u8(layer_priority_);
    w.write_u8(fallback_colour_);
    w.write_u8(transparent_rgb_);
    w.write_u8(sprite_en_ ? 1 : 0);
    w.write_u8(stencil_mode_ ? 1 : 0);
    w.write_u8(tm_enabled_ ? 1 : 0);
    w.write_u8(blend_mode_);
    w.write_bytes(fallback_per_line_.data(), fallback_per_line_.size());
}

void Renderer::load_state(StateReader& r)
{
    ula_.load_state(r);
    layer_priority_ = r.read_u8();
    fallback_colour_ = r.read_u8();
    transparent_rgb_ = r.read_u8();
    sprite_en_    = r.read_u8() != 0;
    stencil_mode_ = r.read_u8() != 0;
    tm_enabled_   = r.read_u8() != 0;
    blend_mode_   = r.read_u8() & 0x03;
    r.read_bytes(fallback_per_line_.data(), fallback_per_line_.size());
}

// ---------------------------------------------------------------------------
// NR 0x15 per-scanline change log (G02)
// ---------------------------------------------------------------------------
//
// Mirrors the PaletteManager / Layer2 idiom.  Driver-side infrastructure
// for demos that toggle layer-priority / sprite-en mid-frame via Copper.
// VHDL zxnext.vhd:5232 (write capture into nr_15_layer_priority +
// nr_15_sprite_en); 6799 (per-line latch into layer_priorities_0).

void Renderer::write_nr15(uint8_t v)
{
    nr15_raw_       = v;
    layer_priority_ = (v >> 2) & 0x07;
    sprite_en_      = (v & 0x01) != 0;

    if (nr15_change_count_ >= MAX_NR15_CHANGES_PER_FRAME) {
        if (!nr15_overflow_warned_) {
            Log::video()->warn(
                "Renderer: NR 0x15 change-log full at line {} (cap {} per "
                "frame); further NR 0x15 writes this frame will not be "
                "per-scanline.",
                nr15_current_line_, MAX_NR15_CHANGES_PER_FRAME);
            nr15_overflow_warned_ = true;
        }
        return;
    }
    nr15_change_log_[nr15_change_count_++] = Nr15Change{
        nr15_current_line_, v,
    };
}

void Renderer::start_frame_nr15()
{
    baseline_nr15_raw_       = nr15_raw_;
    baseline_layer_priority_ = layer_priority_;
    baseline_sprite_en_      = sprite_en_;
    nr15_change_count_       = 0;
    nr15_render_cursor_      = 0;
    nr15_current_line_       = 0;
    nr15_overflow_warned_    = false;
}

void Renderer::rewind_to_baseline_nr15()
{
    nr15_raw_           = baseline_nr15_raw_;
    layer_priority_     = baseline_layer_priority_;
    sprite_en_          = baseline_sprite_en_;
    nr15_render_cursor_ = 0;
}

void Renderer::apply_changes_for_line_nr15(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);
    while (nr15_render_cursor_ < nr15_change_count_
        && nr15_change_log_[nr15_render_cursor_].line == lt) {
        const auto& c = nr15_change_log_[nr15_render_cursor_++];
        nr15_raw_       = c.raw;
        layer_priority_ = (c.raw >> 2) & 0x07;
        sprite_en_      = (c.raw & 0x01) != 0;
    }
}
