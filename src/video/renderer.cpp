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

int Renderer::render_frame(uint32_t* framebuffer, Mmu& mmu, Ram& ram,
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

    // Detect hi-res mode: any 640px layer active this frame?
    hi_res_active_ = (layer2.enabled() && layer2.resolution() >= 2) ||
                     (tilemap && tilemap->enabled() && tilemap->is_80col());
    composite_width_ = hi_res_active_ ? FB_WIDTH_HI : FB_WIDTH;

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

        uint32_t* out = framebuffer + row * composite_width_;
        const int screen_row = row - DISP_Y;  // display row (negative = top border)

        // --- Clear layer buffers to transparent ---
        std::fill_n(layer2_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(sprite_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(tilemap_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(tm_pixel_below_.begin(), composite_width_, false);
        std::fill_n(tm_pixel_textmode_.begin(), composite_width_, false);
        std::fill_n(layer2_priority_.begin(), composite_width_, false);
        std::fill_n(ula_border_.begin(), composite_width_, false);

        // --- Render each layer for this scanline ---
        const bool in_display = (screen_row >= 0 && screen_row < DISP_H);

        // Layer 2 — 256×192 is active only in the display area;
        // 320×256 and 640×256 ("wide") modes cover all 256 rows.
        if (layer2.enabled()) {
            if (layer2.is_wide() || in_display) {
                layer2.render_scanline(layer2_line_.data(), row, ram, palette,
                                       composite_width_, mmu.rom_in_sram());
            }
        }

        // Tilemap — covers the full 320×256 framebuffer (VHDL: vcounter(8)='0')
        if (tilemap && tilemap->enabled()) {
            tilemap->render_scanline(tilemap_line_.data(),
                                     tm_pixel_below_.data(),
                                     row, ram, palette, composite_width_,
                                     tm_pixel_textmode_.data());
        }

        // Sprites — Y coordinates are in absolute framebuffer space (0-255)
        if (sprites && sprites->sprites_visible()) {
            sprites->render_scanline(sprite_line_.data(), row, palette);
        }

        // Render ULA scanline (always 320px).
        const uint32_t fb_argb = rrrgggbb_to_argb(fallback_per_line_[row]);
        ula_.render_scanline(ula_line_.data(), row, mmu);
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
            std::fill_n(ula_line_.begin(), FB_WIDTH, TRANSPARENT);
        }

        // ULA clip window (NextREG 0x1A).
        {
            uint8_t cx1 = ula_.clip_x1();
            uint8_t cx2 = ula_.clip_x2();
            uint8_t cy1 = ula_.clip_y1();
            uint8_t cy2 = ula_.clip_y2();

            bool row_in_display = in_display;
            int vc = screen_row;

            bool y_clipped = row_in_display && (vc < cy1 || vc > cy2);
            bool in_border_y = !row_in_display;

            if (y_clipped) {
                for (int x = DISP_X; x < DISP_X + DISP_W; ++x)
                    ula_line_[x] = TRANSPARENT;
            }

            if (in_border_y || y_clipped) {
                bool clip_border_row;
                if (in_border_y) {
                    clip_border_row = (row < DISP_Y) ? (cy1 > 0) : (cy2 < (DISP_H - 1) || cy1 > cy2 || cy1 >= DISP_H);
                } else {
                    clip_border_row = true;
                }
                if (clip_border_row) {
                    std::fill_n(ula_line_.begin(), FB_WIDTH, TRANSPARENT);
                }
            }

            if (row_in_display && !y_clipped) {
                for (int x = 0; x < DISP_W; ++x) {
                    if (x < cx1 || x > cx2)
                        ula_line_[DISP_X + x] = TRANSPARENT;
                }
                bool left_clipped  = (cx1 > 0);
                bool right_clipped = (cx2 < (DISP_W - 1)) || (cx1 > cx2);
                if (left_clipped) {
                    for (int x = 0; x < DISP_X; ++x)
                        ula_line_[x] = TRANSPARENT;
                }
                if (right_clipped) {
                    for (int x = DISP_X + DISP_W; x < FB_WIDTH; ++x)
                        ula_line_[x] = TRANSPARENT;
                }
            }
        }

        // Pixel-double 320px layers in-place when hi-res is active.
        // Must go right-to-left to avoid overwriting source pixels.
        if (hi_res_active_) {
            // ULA (always 320px) + border flag
            for (int x = FB_WIDTH - 1; x >= 0; --x) {
                ula_line_[x * 2 + 1] = ula_line_[x];
                ula_line_[x * 2]     = ula_line_[x];
                ula_border_[x * 2 + 1] = ula_border_[x];
                ula_border_[x * 2]     = ula_border_[x];
            }

            // Sprites (always 320px)
            for (int x = FB_WIDTH - 1; x >= 0; --x) {
                sprite_line_[x * 2 + 1] = sprite_line_[x];
                sprite_line_[x * 2]     = sprite_line_[x];
            }

            // Layer2: only double if NOT natively 640px
            if (!(layer2.enabled() && layer2.resolution() >= 2)) {
                for (int x = FB_WIDTH - 1; x >= 0; --x) {
                    layer2_line_[x * 2 + 1] = layer2_line_[x];
                    layer2_line_[x * 2]     = layer2_line_[x];
                    layer2_priority_[x * 2 + 1] = layer2_priority_[x];
                    layer2_priority_[x * 2]     = layer2_priority_[x];
                }
            }

            // Tilemap: only double if NOT natively 80-col
            if (!(tilemap && tilemap->enabled() && tilemap->is_80col())) {
                for (int x = FB_WIDTH - 1; x >= 0; --x) {
                    tilemap_line_[x * 2 + 1] = tilemap_line_[x];
                    tilemap_line_[x * 2]     = tilemap_line_[x];
                }
                for (int x = FB_WIDTH - 1; x >= 0; --x) {
                    tm_pixel_below_[x * 2 + 1] = tm_pixel_below_[x];
                    tm_pixel_below_[x * 2]     = tm_pixel_below_[x];
                }
                for (int x = FB_WIDTH - 1; x >= 0; --x) {
                    tm_pixel_textmode_[x * 2 + 1] = tm_pixel_textmode_[x];
                    tm_pixel_textmode_[x * 2]     = tm_pixel_textmode_[x];
                }
            }
        }

        trace_current_row_ = row;
        composite_scanline(out, fb_argb, composite_width_);
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

    // Advance ULA flash state once per frame.
    ula_.advance_flash();

    return composite_width_;
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

void Renderer::composite_scanline(uint32_t* dst, uint32_t fallback_argb, int width)
{
    // Pre-compute NR 0x14 transparency reference (RGB portion only).
    // VHDL 7100: ula_rgb_2(8 downto 1) = transparent_rgb_2
    const uint32_t nr14_rgb = rrrgggbb_to_argb(transparent_rgb_) & 0x00FFFFFF;

    for (int x = 0; x < width; ++x) {
        const uint32_t ula_px  = ula_line_[x];
        const uint32_t l2_px   = layer2_line_[x];
        const uint32_t spr_px  = sprite_line_[x];
        const uint32_t tm_px   = tilemap_line_[x];

        // --- Transparency detection (VHDL 7100-7123) ---
        const bool ula_transp = is_transparent(ula_px) ||
                                ((ula_px & 0x00FFFFFF) == nr14_rgb);
        const bool l2_transp  = is_transparent(l2_px) ||
                                ((l2_px & 0x00FFFFFF) == nr14_rgb);
        // VHDL 6934/7118: sprite_en=0 forces all sprites transparent.
        const bool spr_transp = is_transparent(spr_px) || !sprite_en_;
        // VHDL zxnext.vhd:7109 — `tm_transparent <= '1' when (tm_pixel_en_2 = '0')
        // or (tm_pixel_textmode_2 = '1' and tm_rgb_2(8 downto 1) =
        // transparent_rgb_2) or (tm_en_2 = '0')`.  is_transparent(tm_px)
        // covers both tm_pixel_en_2='0' (suppressed by render_scanline) and
        // tm_en_2='0' (Tilemap::render_scanline early-returns when disabled,
        // leaving the line buffer at TRANSPARENT).  The per-pixel textmode
        // flag gates the RGB match clause (G98 + G101).
        const bool tm_transp  = is_transparent(tm_px) ||
                                (tm_pixel_textmode_[x] &&
                                 (tm_px & 0x00FFFFFF) == nr14_rgb);

        // L2 priority promotion flag (VHDL 7220 etc.: palette bit 15).
        const bool l2_prio = !l2_transp && layer2_priority_[x];
        // Border exception flag (VHDL 7256/7266/7278: ula_border_2).
        const bool ula_border = ula_border_[x];

        // --- ULA/TM merge (VHDL 7112-7116) ---
        uint32_t u_px;
        bool ulatm_transp;
        if (stencil_mode_ && tm_enabled_) {
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

        switch (layer_priority_) {
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

                switch (blend_mode_) {
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
                if (layer_priority_ == 6) {
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
