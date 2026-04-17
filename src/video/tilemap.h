#pragma once
#include <array>
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
    ///   bit 7 = bank select (0 = bank 5, 1 = bank 7).
    ///   bits 5:0 = 256-byte offset within the selected 16K bank.
    ///   bit 6 is unused.
    void set_map_base(uint8_t val);
    uint8_t get_map_base_raw() const { return map_base_raw_; }

    /// NextREG 0x6F — Tile definitions base address (same encoding as 0x6E).
    void set_def_base(uint8_t val);
    uint8_t get_def_base_raw() const { return def_base_raw_; }

    /// NextREG 0x2F — Tilemap X scroll MSB (bits 1:0).
    void set_scroll_x_msb(uint8_t val) { scroll_x_ = (scroll_x_ & 0xFF) | ((val & 0x03) << 8); }

    /// NextREG 0x30 — Tilemap X scroll LSB.
    void set_scroll_x_lsb(uint8_t val) { scroll_x_ = (scroll_x_ & 0x300) | val; }

    /// NextREG 0x31 — Tilemap Y scroll.
    void set_scroll_y(uint8_t val) { scroll_y_ = val; }

    /// Enable/disable tilemap rendering (bit 7 of NextREG 0x6B).
    void set_enabled(bool en) { enabled_ = en; }
    bool enabled() const { return enabled_; }

    /// Snapshot current scroll values for a given scanline (called per-line).
    void snapshot_scroll_for_line(int line) {
        if (line >= 0 && line < 320) {
            scroll_x_per_line_[line] = scroll_x_;
            scroll_y_per_line_[line] = scroll_y_;
        }
    }

    /// Initialize per-line scroll arrays to current values (called at frame start).
    void init_scroll_per_line() {
        scroll_x_per_line_.fill(scroll_x_);
        scroll_y_per_line_.fill(scroll_y_);
    }

    // Clip window (NextREG 0x1B, 4-write cycle)
    void set_clip_x1(uint8_t v) { clip_x1_ = v; }
    void set_clip_x2(uint8_t v) { clip_x2_ = v; }
    void set_clip_y1(uint8_t v) { clip_y1_ = v; }
    void set_clip_y2(uint8_t v) { clip_y2_ = v; }

    // Clip-window getters (VHDL zxnext.vhd:4977-4980 reset defaults:
    // x1=0x00, x2=0x9F, y1=0x00, y2=0xFF).  X coords are internally
    // doubled by the clip comparator (tilemap.vhd:416-417).
    uint8_t clip_x1() const { return clip_x1_; }
    uint8_t clip_x2() const { return clip_x2_; }
    uint8_t clip_y1() const { return clip_y1_; }
    uint8_t clip_y2() const { return clip_y2_; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// True when in 80-column (640-pixel) mode.
    bool is_80col() const { return mode_80col_; }

    /// Render one scanline of tilemap into an ARGB8888 buffer.
    ///
    /// @param dst            Output buffer (render_width pixels wide).
    /// @param ula_over_flags Per-pixel ULA priority flags (render_width entries).
    /// @param y              Display row (0-255 within the active display area).
    /// @param ram            Physical RAM for direct access.
    /// @param palette        Palette manager for tilemap colour lookup.
    /// @param render_width   Output width: 320 or 640. When 640 and 80-col,
    ///                       renders at native 640px resolution (1:1 mapping).
    void render_scanline(uint32_t* dst, bool* ula_over_flags, int y,
                         const Ram& ram,
                         const PaletteManager& palette,
                         int render_width = 320) const;

    /// Render one scanline regardless of enabled_ state. Used by the debugger.
    void render_scanline_debug(uint32_t* dst, bool* ula_over_flags, int y,
                               const Ram& ram,
                               const PaletteManager& palette,
                               int render_width = 320);

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

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

    // Per-scanline scroll snapshots (for mid-frame scroll changes)
    std::array<uint16_t, 320> scroll_x_per_line_{};
    std::array<uint8_t, 320>  scroll_y_per_line_{};

    // Clip window
    uint8_t  clip_x1_        = 0;
    uint8_t  clip_x2_        = 159;     // tilemap default (X coords internally doubled)
    uint8_t  clip_y1_        = 0;
    uint8_t  clip_y2_        = 255;

    // Helpers
    static uint32_t decode_base_addr(uint8_t reg_val);
};
