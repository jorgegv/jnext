#pragma once
#include <cstdint>

class Ram;
class PaletteManager;

/// Layer 2 bitmap renderer (256×192 @ 8-bit colour).
///
/// Layer 2 reads pixel data directly from physical RAM banks, bypassing
/// the MMU.  The active bank (NextREG 0x12) selects the starting 16K bank;
/// three consecutive banks hold the 48K bitmap (3 thirds × 16K each).
///
/// Pixel layout (256×192, 8-bit):
///   Third 0 (rows   0– 63): bank N,   offset = row * 256 + x
///   Third 1 (rows  64–127): bank N+1, offset = (row-64) * 256 + x
///   Third 2 (rows 128–191): bank N+2, offset = (row-128) * 256 + x
///
/// Resolution modes (NextREG 0x70 bits 5:4):
///   00 = 256×192 @ 8-bit  (row-major:    addr = y * 256 + x)
///   01 = 320×256 @ 8-bit  (column-major: addr = x * 256 + y)
///   1x = 640×256 @ 4-bit  (column-major: addr = x * 256 + y, 2 pixels/byte)
///
/// VHDL reference: layer2.vhd (address generation, pixel output).
class Layer2 {
public:
    Layer2() = default;

    void reset();

    // -----------------------------------------------------------------
    // NextREG configuration
    // -----------------------------------------------------------------

    /// NextREG 0x12: active 16K bank (default 8).
    void set_active_bank(uint8_t bank) { active_bank_ = bank & 0x7F; }
    uint8_t active_bank() const { return active_bank_; }

    /// NextREG 0x13: shadow 16K bank (default 11).
    void set_shadow_bank(uint8_t bank) { shadow_bank_ = bank & 0x7F; }
    uint8_t shadow_bank() const { return shadow_bank_; }

    /// NextREG 0x16: X scroll LSB.
    void set_scroll_x_lsb(uint8_t val) { scroll_x_ = (scroll_x_ & 0x100) | val; }

    /// NextREG 0x71: X scroll MSB (bit 0 only).
    void set_scroll_x_msb(uint8_t val) { scroll_x_ = (scroll_x_ & 0xFF) | ((val & 1) << 8); }

    /// NextREG 0x17: Y scroll.
    void set_scroll_y(uint8_t val) { scroll_y_ = val; }

    /// NextREG 0x70: Layer 2 control.
    ///   bits 5:4 = resolution (00=256×192, 01=320×256, 1x=640×256)
    ///   bits 3:0 = palette offset
    void set_control(uint8_t val);

    /// Current resolution mode: 0=256×192, 1=320×256, 2/3=640×256.
    uint8_t resolution() const { return resolution_; }

    /// True when in a wide mode (320×256 or 640×256).
    bool is_wide() const { return resolution_ != 0; }

    /// Enable/disable Layer 2 rendering (from NextREG 0x69 bit 7 or port 0x123B).
    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const { return enabled_; }

    // Clip window (NextREG 0x18, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// Render one scanline of Layer 2 into a 320-pixel ARGB8888 buffer.
    ///
    /// @param dst       Output buffer (320 pixels wide).
    /// @param row       Framebuffer row (0–255).  For 256×192 mode, only
    ///                  rows 32–223 (display area) produce output.  For
    ///                  320×256 / 640×256 modes, all 256 rows are active.
    /// @param ram       Physical RAM for direct bank access.
    /// @param palette   Palette manager for Layer 2 colour lookup.
    void render_scanline(uint32_t* dst, int row, const Ram& ram,
                         const PaletteManager& palette) const;

private:
    uint8_t  active_bank_    = 8;     // NextREG 0x12 default
    uint8_t  shadow_bank_    = 11;    // NextREG 0x13 default
    uint16_t scroll_x_       = 0;     // 9-bit X scroll (0x16 + 0x71)
    uint8_t  scroll_y_       = 0;     // NextREG 0x17
    uint8_t  palette_offset_ = 0;     // bits 3:0 of NextREG 0x70
    uint8_t  resolution_     = 0;     // bits 5:4 of NextREG 0x70 (0=256×192)
    bool     enabled_        = false;
    uint8_t  clip_x1_        = 0;
    uint8_t  clip_x2_        = 255;
    uint8_t  clip_y1_        = 0;
    uint8_t  clip_y2_        = 255;
};
