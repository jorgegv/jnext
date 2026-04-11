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

void Renderer::render_frame(uint32_t* framebuffer, Mmu& mmu, Ram& ram,
                             PaletteManager& palette,
                             Layer2& layer2,
                             SpriteEngine* sprites,
                             Tilemap* tilemap)
{
    for (int row = 0; row < FB_HEIGHT; ++row) {
        uint32_t* out = framebuffer + row * FB_WIDTH;
        const int screen_row = row - DISP_Y;  // display row (negative = top border)

        // --- Step 1: Render ULA scanline ---
        // ULA always produces a full 320-pixel line (border + display).
        // We render it using the existing ULA render_frame logic but
        // into our temporary buffer.  For now, use the full-frame ULA
        // render for the first pass (ULA is always the base layer).

        // --- Step 2: Clear layer buffers to transparent ---
        layer2_line_.fill(TRANSPARENT);
        sprite_line_.fill(TRANSPARENT);
        tilemap_line_.fill(TRANSPARENT);
        ula_over_flags_.fill(false);

        // --- Step 3: Render each layer for this scanline ---
        const bool in_display = (screen_row >= 0 && screen_row < DISP_H);

        // Layer 2 — 256×192 is active only in the display area;
        // 320×256 and 640×256 ("wide") modes cover all 256 rows.
        if (layer2.enabled()) {
            if (layer2.is_wide() || in_display) {
                layer2.render_scanline(layer2_line_.data(), row, ram, palette);
            }
        }

        // Tilemap — covers the full 320×256 framebuffer (VHDL: vcounter(8)='0')
        if (tilemap && tilemap->enabled()) {
            tilemap->render_scanline(tilemap_line_.data(),
                                     ula_over_flags_.data(),
                                     row, ram, palette);
        }

        // Sprites — Y coordinates are in absolute framebuffer space (0-255)
        if (sprites && sprites->sprites_visible()) {
            sprites->render_scanline(sprite_line_.data(), row, palette);
        }

        // --- Step 4: Composite ---
        // ULA is rendered directly first (as the fallback base layer).
        // We'll render ULA into ula_line_, then composite on top.

        // For now, render ULA into the output row first, then overlay.
        // This matches the VHDL where the fallback is always the border/ULA colour.
        // We render ULA into ula_line_ then composite all layers into the output.
        // (ULA render is done per-row using the Ula class.)

        // Temporarily use the output row for ULA rendering, then we'll do
        // proper compositing.  The Ula class's render functions write ARGB
        // pixels directly.

        // We need a scanline-level ULA render.  The existing Ula class only
        // has render_frame().  Let's render ULA into ula_line_ by calling
        // the frame renderer for just this one row.  This is inefficient but
        // correct; we can optimise later.

        // Render ULA scanline.  Always render so border colours are present.
        // When ULA is disabled (NextREG 0x68 bit 7), only the display-area
        // pixels become transparent — border pixels stay as the ULA border
        // colour.  VHDL: ula_en='0' → ula_transparent for display pixels only.
        const uint32_t fb_argb = rrrgggbb_to_argb(fallback_per_line_[row]);
        ula_.render_scanline(ula_line_.data(), row, mmu);
        if (!ula_.ula_enabled() && in_display) {
            // Clear only the display pixels (DISP_X..DISP_X+DISP_W-1) to
            // transparent; border pixels at the left/right margins remain.
            for (int x = DISP_X; x < DISP_X + DISP_W; ++x)
                ula_line_[x] = TRANSPARENT;
        }

        // ULA clip window (NextREG 0x1A).
        // Display pixels outside the clip window become transparent.
        // Border pixels also become transparent when they are adjacent to
        // a fully-clipped axis (matching ZesarUX — allows L2 wide modes
        // to cover the full screen when the ULA is clipped out).
        {
            uint8_t cx1 = ula_.clip_x1();
            uint8_t cx2 = ula_.clip_x2();
            uint8_t cy1 = ula_.clip_y1();
            uint8_t cy2 = ula_.clip_y2();

            bool row_in_display = in_display;
            int vc = screen_row;  // only valid when in_display

            // Is this row's display area clipped?
            bool y_clipped = row_in_display && (vc < cy1 || vc > cy2);
            // Is this row entirely in the border (top/bottom)?
            bool in_border_y = !row_in_display;

            if (y_clipped) {
                // Display row is Y-clipped: clear display pixels
                for (int x = DISP_X; x < DISP_X + DISP_W; ++x)
                    ula_line_[x] = TRANSPARENT;
            }

            if (in_border_y || y_clipped) {
                // Border rows: transparent if the display is fully Y-clipped
                // at this row's corresponding edge. Top border inherits from
                // cy1, bottom from cy2.
                bool clip_border_row;
                if (in_border_y) {
                    // Top border: clip if display top row (vc=0) is outside clip
                    // Bottom border: clip if display bottom row (vc=191) is outside clip
                    // This covers cases like cy1=255,cy2=255 where no display is visible
                    clip_border_row = (row < DISP_Y) ? (cy1 > 0) : (cy2 < (DISP_H - 1) || cy1 > cy2 || cy1 >= DISP_H);
                } else {
                    clip_border_row = true;  // display row is Y-clipped
                }
                if (clip_border_row) {
                    // Clear the entire row — on border rows ALL pixels are
                    // border, including the middle (x=32..287).
                    ula_line_.fill(TRANSPARENT);
                }
            }

            // X-axis clipping for display pixels (on non-Y-clipped rows)
            if (row_in_display && !y_clipped) {
                for (int x = 0; x < DISP_W; ++x) {
                    if (x < cx1 || x > cx2)
                        ula_line_[DISP_X + x] = TRANSPARENT;
                }
                // Clip borders when display edges are clipped.
                // Left border: clip when display left edge is clipped (cx1 > 0).
                // Right border: clip when display right edge is clipped
                // (cx2 < 255) OR when left is so far clipped that effectively
                // all display is gone (cx1 > cx2 means empty window).
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

        composite_scanline(out, fb_argb);
    }

    // Advance ULA flash state once per frame.
    ula_.advance_flash();
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

void Renderer::composite_scanline(uint32_t* dst, uint32_t fallback_argb)
{
    for (int x = 0; x < FB_WIDTH; ++x) {
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
