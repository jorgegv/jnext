#pragma once
#include <array>
#include <cstdint>

class Mmu;
class Ram;
class PaletteManager;

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

    /// Reset ULA state to power-on defaults (preserves palette/RAM pointers).
    void reset() {
        ula_enabled_ = true;
        clip_x1_ = 0; clip_x2_ = 255; clip_y1_ = 0; clip_y2_ = 191;
        border_colour_ = 7;
        border_per_line_.fill(7);
        flash_counter_ = 0;
        flash_phase_ = false;
        screen_mode_reg_ = 0;
        mode_ = TimexScreenMode::STANDARD;
    }

    /// Set the palette manager reference (must be called before rendering).
    void set_palette(PaletteManager* pal) { palette_ = pal; }

    /// Set the RAM reference for direct VRAM access (bypasses MMU, like real hardware).
    void set_ram(Ram* ram) { ram_ = ram; }

    /// Set the border colour (bits 2:0 from ULA port 0xFE write).
    void set_border(uint8_t colour) { border_colour_ = colour & 0x07; }
    uint8_t get_border() const { return border_colour_; }

    /// Snapshot the current border colour for a given scanline.
    /// Called during the frame loop so per-line border changes are preserved.
    void snapshot_border_for_line(int line) {
        if (line >= 0 && line < FB_HEIGHT)
            border_per_line_[line] = border_colour_;
    }

    /// Initialize all per-line border colours to current value (called at frame start).
    void init_border_per_line() {
        border_per_line_.fill(border_colour_);
    }

    /// Get the snapshotted border colour for a given line.
    uint8_t border_for_line(int line) const {
        if (line >= 0 && line < FB_HEIGHT) return border_per_line_[line];
        return border_colour_;
    }

    /// Enable/disable ULA rendering (NextREG 0x68 bit 7).
    void set_ula_enabled(bool enabled) { ula_enabled_ = enabled; }
    bool ula_enabled() const { return ula_enabled_; }

    // Clip window (NextREG 0x1A, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }

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

    /// Render a single scanline from the 128K shadow screen (bank 7, page 14).
    /// Bank 7 uses the same ZX pixel/attribute layout as bank 5 but lives in
    /// physical pages 14-15.  The FPGA implements it as 8K BRAM (enough for
    /// the ~7KB screen data).  Selected by port 0x7FFD bit 3.
    /// Used by the debugger video panel; does NOT affect live rendering state.
    void render_scanline_screen1(uint32_t* dst, int row, Mmu& mmu);

    /// Advance flash state (call once per frame after all scanlines rendered).
    void advance_flash();

private:
    PaletteManager*  palette_         = nullptr; ///< Enhanced palette (falls back to kUlaPalette)
    Ram*             ram_             = nullptr; ///< Physical RAM for direct VRAM reads
    bool             ula_enabled_     = true;  ///< ULA rendering enabled (NextREG 0x68 bit 7)
    bool             vram_use_bank7_  = false; ///< When true, vram_read() reads from bank 7 (page 14) not bank 5 (page 10)
    uint8_t          clip_x1_        = 0;
    uint8_t          clip_x2_        = 255;
    uint8_t          clip_y1_        = 0;
    uint8_t          clip_y2_        = 191;
    uint8_t          border_colour_   = 7;     ///< ZX colour index 0–7 (white = 7)
    std::array<uint8_t, FB_HEIGHT> border_per_line_; ///< Per-scanline border colour snapshots
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

    /// Look up a ULA colour by index (0-15). Uses PaletteManager if available.
    uint32_t lookup_colour(uint8_t idx) const;

    /// Read from ULA VRAM directly from physical bank 5 (or bank 7 for shadow).
    /// On real hardware, the ULA has a dedicated port to bank 5 RAM, bypassing
    /// the MMU entirely. The addr parameter is a CPU-space address (0x4000-0x7FFF);
    /// we convert it to a physical RAM offset in bank 5.
    /// Falls back to MMU reads if RAM is not wired (backward compat).
    uint8_t vram_read(uint16_t addr, Mmu& mmu) const;

    /// Compute the interleaved pixel address offset for (screen_row, col).
    /// Returns the offset from 0x4000 (or 0x6000) for the given position.
    static uint16_t pixel_addr_offset(int screen_row, int col);
};
