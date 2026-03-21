#pragma once
#include <cstdint>

class Ram;
class PaletteManager;

/// Tilemap renderer for 40x32 and 80x32 tile modes.
///
/// The tilemap displays a grid of 8x8 pixel tiles fetched from RAM.
/// In 40-column mode: 40 tiles x 32 rows, each tile 8px wide = 320px.
/// In 80-column mode: 80 tiles x 32 rows, each tile 4px wide = 320px.
///
/// Tile map memory holds either 2 bytes per tile (index + attribute) or
/// 1 byte per tile when flags are stripped (force_attr mode).
///
/// Tile definition memory holds 4-bit-per-pixel patterns: 8x8 = 32 bytes
/// per tile (4 bytes per row, 2 pixels per byte, high nibble first).
///
/// Derived from VHDL: cores/zxnext/src/video/tilemap.vhd
class Tilemap {
public:
    Tilemap() = default;

    void reset();

    // -----------------------------------------------------------------
    // NextREG configuration
    // -----------------------------------------------------------------

    /// NextREG 0x6B — Tilemap control.
    ///   bit 7 = enabled (handled separately, see set_enabled)
    ///   bit 6 = 80-column mode
    ///   bit 5 = text mode (extend palette offset, no rotate/mirror)
    ///   bit 4 = force attribute (use default_attr for all tiles)
    ///   bit 1 = 512 tile mode (attr bit 0 becomes tile index bit 8)
    ///   bit 0 = ULA-over-tilemap (per-tile priority bit)
    void set_control(uint8_t val);
    uint8_t get_control() const { return control_raw_; }

    /// NextREG 0x6C — Default tilemap attribute.
    void set_default_attr(uint8_t val) { default_attr_ = val; }
    uint8_t get_default_attr() const { return default_attr_; }

    /// NextREG 0x6E — Tilemap base address.
    ///   bits 7:1 map to address bits 16:10 (1K boundaries within two 16K banks).
    ///   bit 7 (of the register value, i.e. bit 6 of the 7-bit field) selects
    ///   between bank 5 and bank 7.
    void set_map_base(uint8_t val);
    uint8_t get_map_base_raw() const { return map_base_raw_; }

    /// NextREG 0x6F — Tile definitions base address (same encoding as 0x6E).
    void set_def_base(uint8_t val);
    uint8_t get_def_base_raw() const { return def_base_raw_; }

    /// NextREG 0x2F — Tilemap X scroll LSB.
    void set_scroll_x_lsb(uint8_t val) { scroll_x_ = (scroll_x_ & 0x300) | val; }

    /// NextREG 0x30 — Tilemap X scroll MSB (bits 1:0).
    void set_scroll_x_msb(uint8_t val) { scroll_x_ = (scroll_x_ & 0xFF) | ((val & 0x03) << 8); }

    /// NextREG 0x31 — Tilemap Y scroll.
    void set_scroll_y(uint8_t val) { scroll_y_ = val; }

    /// Enable/disable tilemap rendering (bit 7 of NextREG 0x6B).
    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const { return enabled_; }

    // Clip window (NextREG 0x1B, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// Render one scanline of tilemap into a 320-pixel ARGB8888 buffer.
    ///
    /// @param dst       Output buffer (320 pixels wide, same layout as ULA).
    /// @param y         Display row (0-255 within the active display area).
    /// @param ram       Physical RAM for direct access.
    /// @param palette   Palette manager for tilemap colour lookup.
    ///
    /// The ula_over flags are written into the ula_over_flags array (one bool
    /// per display pixel, 320 entries). When true, the ULA layer should be
    /// drawn on top of this tilemap pixel instead of below it.
    void render_scanline(uint32_t* dst, bool* ula_over_flags, int y,
                         const Ram& ram,
                         const PaletteManager& palette) const;

private:
    // --- Control register state ---
    uint8_t  control_raw_    = 0;
    bool     enabled_        = false;
    bool     mode_80col_     = false;
    bool     text_mode_      = false;
    bool     force_attr_     = false;
    bool     mode_512_       = false;
    bool     ula_on_top_     = false;   // global ULA-over-tilemap flag

    uint8_t  default_attr_   = 0;       // NextREG 0x6C

    // Base addresses (physical RAM byte offsets)
    uint8_t  map_base_raw_   = 0;       // raw register value for 0x6E
    uint8_t  def_base_raw_   = 0;       // raw register value for 0x6F
    uint32_t map_base_addr_  = 0;       // decoded physical RAM address
    uint32_t def_base_addr_  = 0;       // decoded physical RAM address

    // Scroll
    uint16_t scroll_x_       = 0;       // 10-bit X scroll
    uint8_t  scroll_y_       = 0;       // 8-bit Y scroll

    // Clip window
    uint8_t  clip_x1_        = 0;
    uint8_t  clip_x2_        = 159;     // tilemap default (X coords internally doubled)
    uint8_t  clip_y1_        = 0;
    uint8_t  clip_y2_        = 255;

    // Helpers
    static uint32_t decode_base_addr(uint8_t reg_val);
};
