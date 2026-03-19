#include "video/ula.h"
#include "video/palette.h"
#include "memory/mmu.h"
#include "core/log.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// set_screen_mode
// ---------------------------------------------------------------------------

void Ula::set_screen_mode(uint8_t port_val)
{
    screen_mode_reg_ = port_val;

    // Mode is encoded in bits 5:3 of port 0xFF.
    const uint8_t mode_bits = (port_val >> 3) & 0x07;
    switch (mode_bits) {
        case 0: mode_ = TimexScreenMode::STANDARD;   break;
        case 1: mode_ = TimexScreenMode::STANDARD_1; break;
        case 2: mode_ = TimexScreenMode::HI_COLOUR;  break;
        case 6: mode_ = TimexScreenMode::HI_RES;     break;
        default:
            Log::ula()->warn("Unknown Timex screen mode bits {:#04x}, defaulting to STANDARD", mode_bits);
            mode_ = TimexScreenMode::STANDARD;
            break;
    }
    Log::ula()->debug("Screen mode set: port_val={:#04x} mode={}", port_val, static_cast<int>(mode_));
}

// ---------------------------------------------------------------------------
// pixel_addr_offset
// ---------------------------------------------------------------------------
//
// ZX Spectrum screen layout in VRAM (starting at 0x4000):
//
//   Pixel bytes:     0x4000 – 0x57FF  (6 KiB, 192 rows × 32 bytes/row)
//   Attribute bytes: 0x5800 – 0x5AFF  (768 bytes, 24 attr-rows × 32 bytes)
//
// The pixel address is NOT simply row * 32 + col.  The screen is divided into
// three 64-row thirds, and within each third the rows are stored
// interleaved by groups of 8:
//
//   pixel_addr = base
//              | ((screen_row & 0xC0) << 5)   // third select  → bits 12:11
//              | ((screen_row & 0x07) << 8)   // fine row       → bits 10:8
//              | ((screen_row & 0x38) << 2)   // coarse row     → bits 7:5
//              | col                           // column (0–31)  → bits 4:0

uint16_t Ula::pixel_addr_offset(int screen_row, int col)
{
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// ---------------------------------------------------------------------------
// render_frame
// ---------------------------------------------------------------------------

void Ula::render_frame(uint32_t* framebuffer, Mmu& mmu)
{
    // The 320×256 output framebuffer is laid out as follows:
    //
    //   Rows   0..DISP_Y-1              → top border  (DISP_Y = 32)
    //   Rows   DISP_Y..DISP_Y+DISP_H-1 → display area (192 rows)
    //   Rows   DISP_Y+DISP_H..FB_HEIGHT-1 → bottom border
    //
    // Framebuffer height is 256, giving symmetric borders:
    //   top = 32, display = 192, bottom = 256 - 32 - 192 = 32.

    for (int row = 0; row < FB_HEIGHT; ++row) {
        uint32_t* line = framebuffer + row * FB_WIDTH;
        const int screen_row = row - DISP_Y;   // < 0 if in top border

        if (screen_row >= 0 && screen_row < DISP_H) {
            switch (mode_) {
                case TimexScreenMode::STANDARD: {
                    // Primary screen: pixels at 0x4000, attrs at 0x5800.
                    const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                    const uint16_t attr_row = static_cast<uint16_t>(
                        0x5800u + (screen_row / 8) * 32);
                    render_display_line(line, screen_row, poff, attr_row, mmu);
                    break;
                }
                case TimexScreenMode::STANDARD_1: {
                    // Alternate screen: pixels at 0x6000, attrs at 0x7800.
                    const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                    const uint16_t attr_row = static_cast<uint16_t>(
                        0x7800u + (screen_row / 8) * 32);
                    // pixel_base_offset is used as an offset from 0x6000 in
                    // render_display_line — see that function's implementation.
                    render_display_line(line, screen_row, poff, attr_row, mmu);
                    break;
                }
                case TimexScreenMode::HI_COLOUR:
                    render_display_line_hicolour(line, screen_row, mmu);
                    break;
                case TimexScreenMode::HI_RES:
                    render_display_line_hires(line, screen_row, mmu);
                    break;
            }
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
// render_display_line  (STANDARD and STANDARD_1)
// ---------------------------------------------------------------------------
//
// Parameters:
//   pixel_base_offset  — interleaved row offset (from pixel_addr_offset for
//                        column 0); added to 0x4000 (STANDARD) or 0x6000
//                        (STANDARD_1).
//   attr_row_base      — base address of the 32-byte attribute row for this
//                        display line (e.g. 0x5800 + (row/8)*32 for primary,
//                        0x7800 + (row/8)*32 for alternate).
//
// The pixel_base_offset already encodes the interleaved row; column bytes are
// read at pixel_base_offset + col.  For STANDARD_1 the base is shifted to
// 0x6000 — this function receives the base as (0x4000|poff) or (0x6000|poff)
// via the caller, so we keep the same logic.
//
// NOTE: the caller in render_frame passes pixel_addr_offset(screen_row, 0)
// (column-zero offset) and the appropriate attr_row_base.  For STANDARD_1 the
// pixel_base is rebuilt here by OR-ing with 0x6000 instead of 0x4000.

void Ula::render_display_line(uint32_t* row, int screen_row,
                               uint16_t pixel_base_offset,
                               uint16_t attr_row_base,
                               Mmu& mmu)
{
    // Determine the pixel base: if attr_row_base is in the 0x7800 range this
    // is an alternate-screen render; pixel data lives at 0x6000.
    const uint16_t pixel_base = (attr_row_base >= 0x7800)
        ? static_cast<uint16_t>(0x6000u | pixel_base_offset)
        : static_cast<uint16_t>(0x4000u | pixel_base_offset);

    // Fill left border pixels (DISP_X = 32 pixels).
    const uint32_t border_argb = kUlaPalette[border_colour_];
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    // Render the 256 display pixels (32 columns × 8 bits each).
    for (int col = 0; col < 32; ++col) {
        const uint8_t pixels = mmu.read(static_cast<uint16_t>(pixel_base + col));
        const uint8_t attr   = mmu.read(static_cast<uint16_t>(attr_row_base + col));

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
// render_display_line_hicolour  (HI_COLOUR mode)
// ---------------------------------------------------------------------------
//
// Timex hi-colour mode: 256×192 with per-cell (8×1) colour attributes.
//
// Pixel data:    read from primary screen 0x4000 using the standard ZX
//               interleaved addressing.
// Attribute data: each attribute byte controls one 8-pixel column within a
//               single display row (not an 8-row character cell as in standard
//               mode).  The layout of the attribute area at 0x6000 mirrors the
//               standard pixel layout:
//
//                 attr_addr = 0x6000 | pixel_addr_offset(screen_row, col)
//
//               This matches the Timex hi-colour specification: the 6 KiB
//               attribute area starting at 0x6000 uses the same interleaved
//               row addressing as the pixel area at 0x4000, giving one
//               independent attribute byte per 8-pixel column per scanline.
//
// Flash and bright are derived from the attribute byte exactly as in standard
// mode.

void Ula::render_display_line_hicolour(uint32_t* row, int screen_row, Mmu& mmu)
{
    const uint16_t poff = pixel_addr_offset(screen_row, 0);
    const uint16_t pixel_base = static_cast<uint16_t>(0x4000u | poff);
    const uint16_t attr_base  = static_cast<uint16_t>(0x6000u | poff);

    // Fill left border.
    const uint32_t border_argb = kUlaPalette[border_colour_];
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    for (int col = 0; col < 32; ++col) {
        const uint8_t pixels = mmu.read(static_cast<uint16_t>(pixel_base + col));
        const uint8_t attr   = mmu.read(static_cast<uint16_t>(attr_base  + col));

        const bool flash  = (attr & 0x80) != 0;
        const bool bright = (attr & 0x40) != 0;
        uint8_t paper = (attr >> 3) & 0x07;
        uint8_t ink   =  attr       & 0x07;

        if (flash && flash_phase_) {
            uint8_t tmp = ink; ink = paper; paper = tmp;
        }

        const uint8_t ink_idx   = ink   + (bright ? 8 : 0);
        const uint8_t paper_idx = paper + (bright ? 8 : 0);

        const uint32_t ink_argb   = kUlaPalette[ink_idx];
        const uint32_t paper_argb = kUlaPalette[paper_idx];

        uint32_t* dst = row + DISP_X + col * 8;
        for (int bit = 7; bit >= 0; --bit) {
            *dst++ = (pixels >> bit) & 1 ? ink_argb : paper_argb;
        }
    }

    // Fill right border.
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x)
        right[x] = border_argb;
}

// ---------------------------------------------------------------------------
// render_display_line_hires  (HI_RES mode)
// ---------------------------------------------------------------------------
//
// Timex hi-res mode provides 512 monochrome pixels per scanline by
// interleaving two 256-pixel-wide screens:
//   - Screen 0 (0x4000): provides odd display columns (columns 1, 3, 5, …)
//   - Screen 1 (0x6000): provides even display columns (columns 0, 2, 4, …)
//
// LIMITATION: the output framebuffer is only 320 pixels wide (256 active
// display pixels).  A true 512-pixel render does not fit.  This implementation
// renders at 256 output pixels by discarding every second hi-res pixel
// (one output pixel per two hi-res pixels, taken from the most-significant
// bit of each pair).  This halves the horizontal resolution but keeps aspect
// ratio consistent with the other modes.
//
// Ink colour  = bits 2:0 of the last value written to port 0xFF (screen_mode_reg_).
// Paper colour = bits 5:3 of screen_mode_reg_.
// Bright flag is not available in hi-res mode; colours are non-bright (indices 0–7).

void Ula::render_display_line_hires(uint32_t* row, int screen_row, Mmu& mmu)
{
    // Decode ink / paper from port 0xFF register value.
    const uint8_t ink_idx   = screen_mode_reg_ & 0x07;
    const uint8_t paper_idx = (screen_mode_reg_ >> 3) & 0x07;

    const uint32_t ink_argb   = kUlaPalette[ink_idx];
    const uint32_t paper_argb = kUlaPalette[paper_idx];
    const uint32_t border_argb = kUlaPalette[border_colour_];

    // The interleaved pixel row offset is the same for both screens.
    const uint16_t poff = pixel_addr_offset(screen_row, 0);
    const uint16_t screen0_base = static_cast<uint16_t>(0x4000u | poff);
    const uint16_t screen1_base = static_cast<uint16_t>(0x6000u | poff);

    // Fill left border.
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    // Each of the 32 display columns produces 16 hi-res pixels (8 from screen 0,
    // 8 from screen 1, interleaved).  We render them as 8 output pixels (one per
    // hi-res pair) to fit within the 256-pixel display width.
    //
    // Column interleaving: for display column c (0–31):
    //   screen1 byte  → bits 7,5,3,1 of hi-res pixels (even hi-res cols)
    //   screen0 byte  → bits 6,4,2,0 of hi-res pixels (odd hi-res cols)
    //
    // The hi-res columns run left-to-right as: s1_bit7, s0_bit7, s1_bit6,
    // s0_bit6, …  i.e. screen 1 contributes the first pixel of each pair.
    // We output one pixel per pair (the screen-1 pixel = left pixel of pair).

    for (int col = 0; col < 32; ++col) {
        const uint8_t b0 = mmu.read(static_cast<uint16_t>(screen0_base + col));
        const uint8_t b1 = mmu.read(static_cast<uint16_t>(screen1_base + col));

        uint32_t* dst = row + DISP_X + col * 8;
        for (int bit = 7; bit >= 0; --bit) {
            // Take the screen-1 bit as the representative pixel for this pair.
            *dst++ = (b1 >> bit) & 1 ? ink_argb : paper_argb;
        }
    }

    // Fill right border.
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
