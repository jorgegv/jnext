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
// VHDL compositor (zxnext.vhd ~7218):
//   The 3-bit layer_priority value determines the stacking order.
//   Within each mode, layers are checked top-to-bottom: the first
//   non-transparent layer wins.
//
//   The ULA/Tilemap combination: in the VHDL, ULA and Tilemap share the
//   "U" slot.  When tilemap is enabled, tilemap pixels replace ULA pixels
//   (unless the tilemap pixel is transparent, or the per-tile ula_over flag
//   is set, in which case ULA shows through).
//
//   We combine ULA + Tilemap into a single "U" layer before priority
//   compositing to match the VHDL behavior.

void Renderer::composite_scanline(uint32_t* dst, uint32_t fallback_argb, int width)
{
    for (int x = 0; x < width; ++x) {
        const uint32_t ula_px  = ula_line_[x];
        const uint32_t l2_px   = layer2_line_[x];
        const uint32_t spr_px  = sprite_line_[x];
        const uint32_t tm_px   = tilemap_line_[x];

        // Combine ULA + Tilemap into the "U" layer.
        // VHDL: tilemap replaces ULA unless tilemap is transparent or
        // the per-tile ula_over flag is set (ULA has priority over that tile).
        uint32_t u_px;
        if (!is_transparent(tm_px) && !ula_over_flags_[x]) {
            u_px = tm_px;
        } else if (!is_transparent(tm_px) && ula_over_flags_[x]) {
            // ULA over tilemap: use ULA if opaque, else tilemap
            u_px = ula_px;  // ULA is always opaque in standard mode
        } else {
            u_px = ula_px;
        }

        const bool u_opaque   = !is_transparent(u_px);
        const bool l2_opaque  = !is_transparent(l2_px);
        const bool spr_opaque = !is_transparent(spr_px);

        // VHDL: compositor default is the fallback colour (shown when all layers transparent).
        uint32_t result = fallback_argb;

        // Apply priority order: first non-transparent layer wins.
        // The naming convention: S=Sprites, L=Layer2, U=ULA+Tilemap.
        // Order is listed top-to-bottom (highest priority first).
        switch (layer_priority_) {
            case 0:  // SLU — Sprites on top, then Layer2, then ULA
                if (spr_opaque)       result = spr_px;
                else if (l2_opaque)   result = l2_px;
                else if (u_opaque)    result = u_px;
                break;

            case 1:  // LSU — Layer2 on top, then Sprites, then ULA
                if (l2_opaque)        result = l2_px;
                else if (spr_opaque)  result = spr_px;
                else if (u_opaque)    result = u_px;
                break;

            case 2:  // SUL — Sprites on top, then ULA, then Layer2
                if (spr_opaque)       result = spr_px;
                else if (u_opaque)    result = u_px;
                else if (l2_opaque)   result = l2_px;
                break;

            case 3:  // LUS — Layer2 on top, then ULA, then Sprites
                if (l2_opaque)        result = l2_px;
                else if (u_opaque)    result = u_px;
                else if (spr_opaque)  result = spr_px;
                break;

            case 4:  // USL — ULA on top, then Sprites, then Layer2
                if (u_opaque)         result = u_px;
                else if (spr_opaque)  result = spr_px;
                else if (l2_opaque)   result = l2_px;
                break;

            case 5:  // ULS — ULA on top, then Layer2, then Sprites
                if (u_opaque)         result = u_px;
                else if (l2_opaque)   result = l2_px;
                else if (spr_opaque)  result = spr_px;
                break;

            default:  // Modes 6,7 are blending (deferred to Phase 8)
                // Fall back to SLU for now.
                if (spr_opaque)       result = spr_px;
                else if (l2_opaque)   result = l2_px;
                else if (u_opaque)    result = u_px;
                break;
        }

        dst[x] = result;
    }
}

void Renderer::save_state(StateWriter& w) const
{
    ula_.save_state(w);
    w.write_u8(layer_priority_);
    w.write_u8(fallback_colour_);
    w.write_bytes(fallback_per_line_.data(), fallback_per_line_.size());
}

void Renderer::load_state(StateReader& r)
{
    ula_.load_state(r);
    layer_priority_ = r.read_u8();
    fallback_colour_ = r.read_u8();
    r.read_bytes(fallback_per_line_.data(), fallback_per_line_.size());
}
