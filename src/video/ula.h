#pragma once
#include <cstdint>

class Mmu;

/// ULA pixel renderer.
///
/// Renders the full 320×256 output frame into a uint32_t (ARGB8888) framebuffer
/// by reading pixel data and attributes from VRAM via the MMU.
///
/// Layout of the 320×256 output framebuffer:
///
///   ┌──────────────────────────────────────┐
///   │         48px top border              │  rows 0–47
///   ├────┬─────────────────────────┬───────┤
///   │ 32 │  256×192 display area   │  32   │  rows 48–239
///   │ px │                         │  px   │
///   ├────┴─────────────────────────┴───────┤
///   │         56px bottom border           │  rows 240–255 (last 16 rows
///   └──────────────────────────────────────┘  exceed 256 — clipped)
///
/// Left/right borders are each 32 pixels wide in the output (the ZX hardware
/// has 48px physical border but the output framebuffer is only 320 px wide:
/// 32 + 256 + 32 = 320).
class Ula {
public:
    // Output framebuffer dimensions
    static constexpr int FB_WIDTH   = 320;
    static constexpr int FB_HEIGHT  = 256;

    // Active display area within the framebuffer
    static constexpr int DISP_X     = 32;   // left border width in output pixels
    static constexpr int DISP_Y     = 48;   // top border height in output pixels
    static constexpr int DISP_W     = 256;
    static constexpr int DISP_H     = 192;

    /// Set the border colour (bits 2:0 from ULA port 0xFE write).
    void set_border(uint8_t colour) { border_colour_ = colour & 0x07; }
    uint8_t get_border() const { return border_colour_; }

    /// Render one complete frame.
    ///
    /// @param framebuffer  Pointer to FB_WIDTH × FB_HEIGHT ARGB8888 pixels,
    ///                     row-major (row 0 = top).
    /// @param mmu          MMU for reading VRAM (screen at 0x4000, attrs at 0x5800).
    void render_frame(uint32_t* framebuffer, Mmu& mmu);

private:
    uint8_t border_colour_ = 7;     ///< ZX colour index 0–7 (white = 7)
    int     flash_counter_ = 0;     ///< Incremented once per frame
    bool    flash_phase_   = false; ///< Toggles every 16 frames

    /// Render a display row into the given row buffer.
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param mmu         MMU for VRAM access.
    void render_display_line(uint32_t* row, int screen_row, Mmu& mmu);

    /// Fill an entire output row with the border colour.
    /// @param row  Pointer to the start of the output row (FB_WIDTH pixels).
    void render_border_line(uint32_t* row);
};
