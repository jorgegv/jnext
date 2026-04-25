#include "video/renderer.h"
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

    for (int row = 0; row < FB_HEIGHT; ++row) {
        // Replay log entries tagged with this scanline before any
        // layer rendering reads palette state.
        palette.apply_changes_for_line(row);
        layer2.apply_changes_for_line(row);

        uint32_t* out = framebuffer + row * composite_width_;
        const int screen_row = row - DISP_Y;  // display row (negative = top border)

        // --- Clear layer buffers to transparent ---
        std::fill_n(layer2_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(sprite_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(tilemap_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(tm_pixel_below_.begin(), composite_width_, false);
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
                                     row, ram, palette, composite_width_);
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
            }
        }

        composite_scanline(out, fb_argb, composite_width_);
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
        const bool tm_transp  = is_transparent(tm_px);

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
