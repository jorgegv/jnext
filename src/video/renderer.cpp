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

    for (int row = 0; row < FB_HEIGHT; ++row) {
        uint32_t* out = framebuffer + row * composite_width_;
        const int screen_row = row - DISP_Y;  // display row (negative = top border)

        // --- Clear layer buffers to transparent ---
        std::fill_n(layer2_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(sprite_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(tilemap_line_.begin(), composite_width_, TRANSPARENT);
        std::fill_n(ula_over_flags_.begin(), composite_width_, false);

        // --- Render each layer for this scanline ---
        const bool in_display = (screen_row >= 0 && screen_row < DISP_H);

        // Layer 2 — 256×192 is active only in the display area;
        // 320×256 and 640×256 ("wide") modes cover all 256 rows.
        if (layer2.enabled()) {
            if (layer2.is_wide() || in_display) {
                layer2.render_scanline(layer2_line_.data(), row, ram, palette,
                                       composite_width_);
            }
        }

        // Tilemap — covers the full 320×256 framebuffer (VHDL: vcounter(8)='0')
        if (tilemap && tilemap->enabled()) {
            tilemap->render_scanline(tilemap_line_.data(),
                                     ula_over_flags_.data(),
                                     row, ram, palette, composite_width_);
        }

        // Sprites — Y coordinates are in absolute framebuffer space (0-255)
        if (sprites && sprites->sprites_visible()) {
            sprites->render_scanline(sprite_line_.data(), row, palette);
        }

        // Render ULA scanline (always 320px).
        const uint32_t fb_argb = rrrgggbb_to_argb(fallback_per_line_[row]);
        ula_.render_scanline(ula_line_.data(), row, mmu);
        if (!ula_.ula_enabled() && in_display) {
            for (int x = DISP_X; x < DISP_X + DISP_W; ++x)
                ula_line_[x] = TRANSPARENT;
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
            // ULA (always 320px)
            for (int x = FB_WIDTH - 1; x >= 0; --x) {
                ula_line_[x * 2 + 1] = ula_line_[x];
                ula_line_[x * 2]     = ula_line_[x];
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
                }
            }

            // Tilemap: only double if NOT natively 80-col
            if (!(tilemap && tilemap->enabled() && tilemap->is_80col())) {
                for (int x = FB_WIDTH - 1; x >= 0; --x) {
                    tilemap_line_[x * 2 + 1] = tilemap_line_[x];
                    tilemap_line_[x * 2]     = tilemap_line_[x];
                }
                // Also double the ula_over flags
                for (int x = FB_WIDTH - 1; x >= 0; --x) {
                    ula_over_flags_[x * 2 + 1] = ula_over_flags_[x];
                    ula_over_flags_[x * 2]     = ula_over_flags_[x];
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
//     mix_rgb/mix_top/mix_bot from ULA blend mode (default "00"):
//       mix_rgb = ULA (zeroed when transparent)
//       mix_top = TM (when above), mix_bot = TM (when below)
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
        const bool spr_transp = is_transparent(spr_px);
        const bool tm_transp  = is_transparent(tm_px);

        // --- ULA/TM merge (VHDL 7115-7116) ---
        // ulatm_transparent = ula_transparent AND tm_transparent
        const bool ulatm_transp = ula_transp && tm_transp;
        // ulatm_rgb: TM when TM opaque AND (above OR ULA transparent), else ULA
        uint32_t u_px;
        if (!tm_transp && (!ula_over_flags_[x] || ula_transp)) {
            u_px = tm_px;
        } else {
            u_px = ula_transp ? TRANSPARENT : ula_px;
        }

        uint32_t result = fallback_argb;

        switch (layer_priority_) {
            case 0:  // SLU (VHDL 7218)
                if (!spr_transp)        result = spr_px;
                else if (!l2_transp)    result = l2_px;
                else if (!ulatm_transp) result = u_px;
                break;

            case 1:  // LSU (VHDL 7230)
                if (!l2_transp)         result = l2_px;
                else if (!spr_transp)   result = spr_px;
                else if (!ulatm_transp) result = u_px;
                break;

            case 2:  // SUL (VHDL 7240)
                if (!spr_transp)        result = spr_px;
                else if (!ulatm_transp) result = u_px;
                else if (!l2_transp)    result = l2_px;
                break;

            case 3:  // LUS (VHDL 7252)
                if (!l2_transp)         result = l2_px;
                else if (!ulatm_transp) result = u_px;
                else if (!spr_transp)   result = spr_px;
                break;

            case 4:  // USL (VHDL 7262)
                if (!ulatm_transp)      result = u_px;
                else if (!spr_transp)   result = spr_px;
                else if (!l2_transp)    result = l2_px;
                break;

            case 5:  // ULS (VHDL 7274)
                if (!ulatm_transp)      result = u_px;
                else if (!l2_transp)    result = l2_px;
                else if (!spr_transp)   result = spr_px;
                break;

            case 6: {  // Blend additive — VHDL 7286-7310
                // ULA blend mode "00" (default): mix_rgb=ULA, mix_top=TM
                const bool mix_rgb_transp = ula_transp;
                const bool mix_top_transp = tm_transp || ula_over_flags_[x];
                const bool mix_bot_transp = tm_transp || !ula_over_flags_[x];

                // Extract 3/3/2 channels (zeroed when transparent per VHDL 7101/7122)
                const uint8_t l2_r = l2_transp ? 0 : argb_r3(l2_px);
                const uint8_t l2_g = l2_transp ? 0 : argb_g3(l2_px);
                const uint8_t l2_b = l2_transp ? 0 : argb_b2(l2_px);
                const uint8_t mx_r = mix_rgb_transp ? 0 : argb_r3(ula_px);
                const uint8_t mx_g = mix_rgb_transp ? 0 : argb_g3(ula_px);
                const uint8_t mx_b = mix_rgb_transp ? 0 : argb_b2(ula_px);

                // Per-channel add with clamp (VHDL 7288-7298)
                const uint8_t r_out = std::min<uint8_t>(l2_r + mx_r, 7);
                const uint8_t g_out = std::min<uint8_t>(l2_g + mx_g, 7);
                const uint8_t b_out = std::min<uint8_t>(l2_b + mx_b, 3);
                const uint32_t mixer_argb = channels_to_argb(r_out, g_out, b_out);

                // Output chain (VHDL 7300-7310)
                // L2 priority promotion skipped (not yet implemented)
                if      (!mix_top_transp) result = tm_px;
                else if (!spr_transp)     result = spr_px;
                else if (!mix_bot_transp) result = tm_px;
                else if (!l2_transp)      result = mixer_argb;
                break;
            }

            default: {  // Blend subtractive (mode 7) — VHDL 7312-7352
                const bool mix_rgb_transp = ula_transp;
                const bool mix_top_transp = tm_transp || ula_over_flags_[x];
                const bool mix_bot_transp = tm_transp || !ula_over_flags_[x];

                const uint8_t l2_r = l2_transp ? 0 : argb_r3(l2_px);
                const uint8_t l2_g = l2_transp ? 0 : argb_g3(l2_px);
                const uint8_t l2_b = l2_transp ? 0 : argb_b2(l2_px);
                const uint8_t mx_r = mix_rgb_transp ? 0 : argb_r3(ula_px);
                const uint8_t mx_g = mix_rgb_transp ? 0 : argb_g3(ula_px);
                const uint8_t mx_b = mix_rgb_transp ? 0 : argb_b2(ula_px);

                // Raw per-channel sum
                uint8_t r_sum = l2_r + mx_r;
                uint8_t g_sum = l2_g + mx_g;
                uint8_t b_sum = l2_b + mx_b;

                // Subtraction formula only when mix_rgb not transparent (VHDL 7314)
                if (!mix_rgb_transp) {
                    auto sub = [](uint8_t s) -> uint8_t {
                        if (s <= 4) return 0;
                        if (((s >> 2) & 3) == 3) return 7;  // >= 12
                        return static_cast<uint8_t>((s + 0x0Bu) & 0x0Fu);  // sum - 5
                    };
                    r_sum = sub(r_sum);
                    g_sum = sub(g_sum);
                    b_sum = sub(b_sum);
                }

                const uint32_t mixer_argb = channels_to_argb(r_sum & 7, g_sum & 7, b_sum & 3);

                // Output chain (VHDL 7342-7352)
                if      (!mix_top_transp) result = tm_px;
                else if (!spr_transp)     result = spr_px;
                else if (!mix_bot_transp) result = tm_px;
                else if (!l2_transp)      result = mixer_argb;
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
    w.write_bytes(fallback_per_line_.data(), fallback_per_line_.size());
}

void Renderer::load_state(StateReader& r)
{
    ula_.load_state(r);
    layer_priority_ = r.read_u8();
    fallback_colour_ = r.read_u8();
    transparent_rgb_ = r.read_u8();
    r.read_bytes(fallback_per_line_.data(), fallback_per_line_.size());
}
