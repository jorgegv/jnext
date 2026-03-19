#include "video/layer2.h"
#include "video/palette.h"
#include "memory/ram.h"

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void Layer2::reset()
{
    active_bank_    = 8;
    shadow_bank_    = 11;
    scroll_x_       = 0;
    scroll_y_       = 0;
    palette_offset_ = 0;
    resolution_     = 0;
    enabled_        = false;
}

// ---------------------------------------------------------------------------
// NextREG 0x70 — Layer 2 control
// ---------------------------------------------------------------------------

void Layer2::set_control(uint8_t val)
{
    resolution_     = (val >> 4) & 0x03;
    palette_offset_ = val & 0x0F;
}

// ---------------------------------------------------------------------------
// Render one scanline (256×192, 8-bit colour)
// ---------------------------------------------------------------------------

void Layer2::render_scanline(uint32_t* dst, int y, const Ram& ram,
                             const PaletteManager& palette) const
{
    if (!enabled_ || y < 0 || y >= 192)
        return;

    // Only 256×192 @ 8-bit is implemented; other resolutions are no-ops.
    if (resolution_ != 0)
        return;

    // Apply Y scroll (wraps within 192 rows).
    int src_y = (y + scroll_y_) % 192;

    // Determine which 16K bank and offset within it.
    // Third 0 = rows 0-63 (bank N), Third 1 = 64-127 (bank N+1), Third 2 = 128-191 (bank N+2).
    int third = src_y / 64;
    int row_in_third = src_y % 64;
    uint8_t bank = active_bank_ + third;

    // Convert 16K bank to physical RAM address.
    // 16K bank N starts at byte offset N * 16384 in RAM.
    uint32_t bank_base = static_cast<uint32_t>(bank) * 16384;
    uint32_t row_offset = static_cast<uint32_t>(row_in_third) * 256;

    // The output framebuffer has 32px left border + 256px display + 32px right border.
    // Layer 2 only writes into the 256px display area (offset 32).
    static constexpr int DISP_X = 32;

    uint8_t transparency = palette.global_transparency();

    for (int x = 0; x < 256; ++x) {
        // Apply X scroll (wraps within 256 pixels).
        int src_x = (x + (scroll_x_ & 0xFF)) & 0xFF;

        uint32_t addr = bank_base + row_offset + src_x;
        uint8_t pixel = ram.read(addr);

        // Skip transparent pixels.
        if (pixel == transparency)
            continue;

        // Apply palette offset to the high nibble.
        uint8_t colour_idx = static_cast<uint8_t>(
            ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

        dst[DISP_X + x] = palette.layer2_colour(colour_idx);
    }
}
