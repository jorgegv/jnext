#include "video/ula.h"
#include "video/palette.h"
#include "memory/mmu.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// render_frame
// ---------------------------------------------------------------------------

void Ula::render_frame(uint32_t* framebuffer, Mmu& mmu)
{
    // The 320×256 output framebuffer is laid out as follows:
    //
    //   Rows   0..DISP_Y-1            → top border  (DISP_Y = 48)
    //   Rows   DISP_Y..DISP_Y+DISP_H-1 → display area (192 rows)
    //   Rows   DISP_Y+DISP_H..FB_HEIGHT-1 → bottom border
    //
    // Framebuffer height is exactly 256, so the bottom border is
    // FB_HEIGHT - (DISP_Y + DISP_H) = 256 - 240 = 16 rows of output
    // (the physical ZX border has 56 lines but we clip to fit 256 rows).

    const uint32_t border_argb = kUlaPalette[border_colour_];

    for (int row = 0; row < FB_HEIGHT; ++row) {
        uint32_t* line = framebuffer + row * FB_WIDTH;
        const int screen_row = row - DISP_Y;   // < 0 if in top border

        if (screen_row >= 0 && screen_row < DISP_H) {
            render_display_line(line, screen_row, mmu);
        } else {
            // Top or bottom border: fill entire row with border colour.
            render_border_line(line);
        }
    }

    // Advance flash state once per frame.
    ++flash_counter_;
    if (flash_counter_ >= 16) {
        flash_counter_ = 0;
        flash_phase_   = !flash_phase_;
    }
}

// ---------------------------------------------------------------------------
// render_display_line
// ---------------------------------------------------------------------------
//
// ZX Spectrum screen layout in VRAM (starting at 0x4000):
//
//   Pixel bytes:   0x4000 – 0x57FF  (6 KiB, 192 rows × 32 bytes/row)
//   Attribute bytes: 0x5800 – 0x5AFF  (768 bytes, 24 attr-rows × 32 bytes)
//
// The pixel address is NOT simply row * 32 + col.  The screen is divided into
// three 64-row thirds, and within each third the rows are stored
// interleaved by groups of 8:
//
//   pixel_addr = 0x4000
//              | ((screen_row & 0xC0) << 5)   // third select  → bits 12:11
//              | ((screen_row & 0x07) << 8)   // fine row       → bits 10:8
//              | ((screen_row & 0x38) << 2)   // coarse row     → bits 7:5
//              | col                           // column (0–31)  → bits 4:0
//
// Each attribute byte controls an 8×8 cell:
//   bit 7: flash
//   bit 6: bright
//   bits 5:3: paper colour (0–7)
//   bits 2:0: ink colour (0–7)

void Ula::render_display_line(uint32_t* row, int screen_row, Mmu& mmu)
{
    // Base VRAM addresses.
    const uint16_t pixel_base =
        static_cast<uint16_t>(0x4000u
          | static_cast<unsigned>((screen_row & 0xC0) << 5)
          | static_cast<unsigned>((screen_row & 0x07) << 8)
          | static_cast<unsigned>((screen_row & 0x38) << 2));

    const uint16_t attr_base =
        static_cast<uint16_t>(0x5800u + (screen_row / 8) * 32);

    // Fill left border pixels (DISP_X = 32 pixels).
    const uint32_t border_argb = kUlaPalette[border_colour_];
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    // Render the 256 display pixels (32 columns × 8 bits each).
    for (int col = 0; col < 32; ++col) {
        const uint8_t pixels = mmu.read(static_cast<uint16_t>(pixel_base + col));
        const uint8_t attr   = mmu.read(static_cast<uint16_t>(attr_base   + col));

        const bool flash  = (attr & 0x80) != 0;
        const bool bright = (attr & 0x40) != 0;
        uint8_t paper = (attr >> 3) & 0x07;
        uint8_t ink   =  attr       & 0x07;

        // Flash: swap ink/paper on the active phase.
        if (flash && flash_phase_) {
            uint8_t tmp = ink; ink = paper; paper = tmp;
        }

        // Apply bright flag: bright colours have index in [8,15].
        const uint8_t ink_idx   = ink   + (bright ? 8 : 0);
        const uint8_t paper_idx = paper + (bright ? 8 : 0);

        const uint32_t ink_argb   = kUlaPalette[ink_idx];
        const uint32_t paper_argb = kUlaPalette[paper_idx];

        // Write 8 pixels; pixel bit 7 is the leftmost pixel.
        uint32_t* dst = row + DISP_X + col * 8;
        for (int bit = 7; bit >= 0; --bit) {
            *dst++ = (pixels >> bit) & 1 ? ink_argb : paper_argb;
        }
    }

    // Fill right border pixels (FB_WIDTH - DISP_X - DISP_W = 32 pixels).
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x)
        right[x] = border_argb;
}

// ---------------------------------------------------------------------------
// render_border_line
// ---------------------------------------------------------------------------

void Ula::render_border_line(uint32_t* row)
{
    const uint32_t border_argb = kUlaPalette[border_colour_];
    for (int x = 0; x < FB_WIDTH; ++x)
        row[x] = border_argb;
}
