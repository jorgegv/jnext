#include "video/renderer.h"
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

        if (in_display) {
            // Layer 2
            if (layer2.enabled()) {
                layer2.render_scanline(layer2_line_.data(), screen_row, ram, palette);
            }

            // Tilemap
            if (tilemap && tilemap->enabled()) {
                tilemap->render_scanline(tilemap_line_.data(),
                                         ula_over_flags_.data(),
                                         screen_row, ram, palette);
            }

            // Sprites
            if (sprites && sprites->sprites_visible()) {
                sprites->render_scanline(sprite_line_.data(), screen_row, palette);
            }
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

        // Actually, let's just inline the ULA scanline render here using
        // the Ula's internal state:
        ula_.render_scanline(ula_line_.data(), row, mmu);

        composite_scanline(out);
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

void Renderer::composite_scanline(uint32_t* dst)
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

        // Default fallback is ULA (always has a colour — border or display).
        uint32_t result = u_px;

        // Apply priority order: first non-transparent layer wins.
        // The naming convention: S=Sprites, L=Layer2, U=ULA+Tilemap.
        // Order is listed top-to-bottom (highest priority first).
        switch (layer_priority_) {
            case 0:  // SLU — Sprites on top, then Layer2, then ULA
                if (spr_opaque)       result = spr_px;
                else if (l2_opaque)   result = l2_px;
                else                  result = u_px;
                break;

            case 1:  // LSU — Layer2 on top, then Sprites, then ULA
                if (l2_opaque)        result = l2_px;
                else if (spr_opaque)  result = spr_px;
                else                  result = u_px;
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
                else                  result = u_px;
                break;
        }

        dst[x] = result;
    }
}
