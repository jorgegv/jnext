#pragma once
#include <cstdint>

class Mmu;

/// Timex ULA screen mode, selected via port 0xFF.
///
/// Port 0xFF bit layout:
///   bits 2:0  = screen bank (0 = primary 0x4000, 1 = alternate 0x6000)
///   bits 5:3  = video mode
///
/// Only the mode bits (5:3) are decoded here; the screen bank field is used by
/// STANDARD_1 mode and is implicit in HI_COLOUR / HI_RES.
enum class TimexScreenMode : uint8_t {
    STANDARD   = 0,  ///< Normal ULA 256×192; pixel+attr from 0x4000/0x5800
    STANDARD_1 = 1,  ///< Alternate screen: pixel+attr from 0x6000/0x7800
    HI_COLOUR  = 2,  ///< 256×192; pixels from 0x4000, per-row-column attr from 0x6000
    HI_RES     = 6,  ///< 512-wide monochrome (rendered at 256 pixels — see note below)
};

/// ULA pixel renderer.
///
/// Renders the full 320×256 output frame into a uint32_t (ARGB8888) framebuffer
/// by reading pixel data and attributes from VRAM via the MMU.
///
/// Layout of the 320×256 output framebuffer:
///
///   ┌──────────────────────────────────────┐
///   │         32px top border              │  rows 0–31
///   ├────┬─────────────────────────┬───────┤
///   │ 32 │  256×192 display area   │  32   │  rows 32–223
///   │ px │                         │  px   │
///   ├────┴─────────────────────────┴───────┤
///   │         32px bottom border           │  rows 224–255
///   └──────────────────────────────────────┘
///
/// Left/right borders are each 32 pixels wide (32 + 256 + 32 = 320).
/// Top and bottom borders are each 32 rows (32 + 192 + 32 = 256), symmetric.
class Ula {
public:
    // Output framebuffer dimensions
    static constexpr int FB_WIDTH   = 320;
    static constexpr int FB_HEIGHT  = 256;

    // Active display area within the framebuffer
    static constexpr int DISP_X     = 32;   // left border width in output pixels
    static constexpr int DISP_Y     = 32;   // top border height in output pixels
    static constexpr int DISP_W     = 256;
    static constexpr int DISP_H     = 192;

    /// Set the border colour (bits 2:0 from ULA port 0xFE write).
    void set_border(uint8_t colour) { border_colour_ = colour & 0x07; }
    uint8_t get_border() const { return border_colour_; }

    /// Enable/disable ULA rendering (NextREG 0x68 bit 7).
    void set_ula_enabled(bool enabled) { ula_enabled_ = enabled; }
    bool ula_enabled() const { return ula_enabled_; }

    /// Set the Timex screen mode from a port 0xFF write.
    ///
    /// The full byte is stored so callers can read it back (e.g. for
    /// floating-bus emulation).  The mode is decoded from bits 5:3.
    void set_screen_mode(uint8_t port_val);

    /// Return the raw byte last written to port 0xFF.
    uint8_t get_screen_mode_reg() const { return screen_mode_reg_; }

    /// Render one complete frame.
    ///
    /// @param framebuffer  Pointer to FB_WIDTH × FB_HEIGHT ARGB8888 pixels,
    ///                     row-major (row 0 = top).
    /// @param mmu          MMU for reading VRAM (screen at 0x4000, attrs at 0x5800).
    void render_frame(uint32_t* framebuffer, Mmu& mmu);

    /// Render a single scanline (row 0..FB_HEIGHT-1) into a 320-pixel buffer.
    /// Used by the compositor for per-scanline rendering.
    void render_scanline(uint32_t* dst, int row, Mmu& mmu);

    /// Advance flash state (call once per frame after all scanlines rendered).
    void advance_flash();

private:
    bool             ula_enabled_     = true;  ///< ULA rendering enabled (NextREG 0x68 bit 7)
    uint8_t          border_colour_   = 7;     ///< ZX colour index 0–7 (white = 7)
    int              flash_counter_   = 0;     ///< Incremented once per frame
    bool             flash_phase_     = false; ///< Toggles every 16 frames
    uint8_t          screen_mode_reg_ = 0;     ///< Raw value last written to port 0xFF
    TimexScreenMode  mode_            = TimexScreenMode::STANDARD;

    /// Render a display row in STANDARD or STANDARD_1 mode.
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param pixel_base  Base address of pixel data (0x4000 or 0x6000).
    /// @param attr_base_row  Base address of attribute row: 0x5800+(row/8)*32
    ///                       or 0x7800+(row/8)*32 for alternate screen.
    /// @param mmu         MMU for VRAM access.
    void render_display_line(uint32_t* row, int screen_row,
                             uint16_t pixel_base_offset,
                             uint16_t attr_row_base,
                             Mmu& mmu);

    /// Render a display row in HI_COLOUR mode.
    /// Pixel data from primary screen (0x4000), per-row-column attribute from
    /// alternate screen (0x6000).  Attribute layout mirrors the standard pixel
    /// layout: attr(row, col) = mmu.read(0x6000 | pixel_addr_offset(row, col)).
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param mmu         MMU for VRAM access.
    void render_display_line_hicolour(uint32_t* row, int screen_row, Mmu& mmu);

    /// Render a display row in HI_RES mode.
    ///
    /// True Timex hi-res is 512 pixels wide (monochrome).  The framebuffer is
    /// only 320 pixels wide, so we render at 256 output pixels by taking one
    /// output pixel per two hi-res pixels (every other hi-res pixel is dropped).
    /// This is a known limitation; a future revision could offer a scrolled
    /// or scaled view.
    ///
    /// Ink colour = bits 2:0 of port 0xFF; paper = bits 5:3.
    /// @param row         Pointer to the start of the output row (FB_WIDTH pixels).
    /// @param screen_row  ZX screen row [0, 191].
    /// @param mmu         MMU for VRAM access.
    void render_display_line_hires(uint32_t* row, int screen_row, Mmu& mmu);

    /// Fill an entire output row with the border colour.
    /// @param row  Pointer to the start of the output row (FB_WIDTH pixels).
    void render_border_line(uint32_t* row);

    /// Compute the interleaved pixel address offset for (screen_row, col).
    /// Returns the offset from 0x4000 (or 0x6000) for the given position.
    static uint16_t pixel_addr_offset(int screen_row, int col);
};
