#pragma once
#include <cstdint>
#include <cstring>

class Ram;
class PaletteManager;

/// ZX Spectrum Next sprite engine — 128 hardware sprites, 16x16 pixels each.
///
/// Implements the sprite subsystem as defined in the FPGA VHDL (sprites.vhd).
///
/// Sprite attribute format (5 bytes per sprite, written via port 0x57):
///   Byte 0: X position bits 7:0
///   Byte 1: Y position bits 7:0
///   Byte 2: bits 7:4 = palette offset, bit 3 = X mirror, bit 2 = Y mirror,
///           bit 1 = rotate, bit 0 = X MSB (bit 8)
///   Byte 3: bit 7 = visible, bit 6 = extended (5th byte present),
///           bits 5:0 = pattern index N5:N0
///   Byte 4: bit 7 = 4-bit colour (H flag), bit 6 = N6 (7th pattern bit),
///           bit 5 = reserved, bits 4:3 = X scale, bits 2:1 = Y scale,
///           bit 0 = Y MSB (bit 8)
///
/// When byte 3 bit 6 is clear (no extended attributes), only 4 bytes are
/// written per sprite and byte 4 retains its previous value.  The Y MSB is
/// forced to 0 in that case (hardware: spr_y8 = 0 when attr3(6)=0).
///
/// Pattern memory is 16 KB (16384 bytes), addressed as:
///   8-bit mode: pattern[6:0] & row[3:0] & col[3:0]  (256 bytes per pattern)
///   4-bit mode: pattern[6:1] & N6 & row[3:0] & col[3:1] (128 bytes per pattern)
///
/// Per-sprite X/Y scaling (x1/x2/x4/x8) is implemented via extended byte 4.
/// Sprite anchoring (composite/relative sprites) is implemented.
class SpriteEngine {
public:
    static constexpr int NUM_SPRITES    = 128;
    static constexpr int PATTERN_RAM_SZ = 16384;  // 16 KB, 14-bit address
    static constexpr int SPRITE_SIZE    = 16;
    static constexpr int DISPLAY_WIDTH  = 320;     // full pixel width including border

    SpriteEngine() { reset(); }

    void reset();

    // -----------------------------------------------------------------
    // Port handlers
    // -----------------------------------------------------------------

    /// Port 0x303B write: select sprite slot for attribute/pattern writes.
    ///   bits 6:0 = sprite index (0-127)
    ///   bit 7    = pattern slot high bit (pattern_index bit 7)
    void write_slot_select(uint8_t val);

    /// Port 0x303B read: sprite status register.
    ///   bit 1 = max sprites per line exceeded (sticky, cleared on read)
    ///   bit 0 = collision detected (sticky, cleared on read)
    uint8_t read_status();

    /// Port 0x57 write: auto-incrementing sprite attribute upload.
    /// Writes 4 or 5 bytes per sprite depending on byte 3 bit 6 (extended).
    void write_attribute(uint8_t val);

    /// Port 0x5B write: auto-incrementing pattern data upload.
    void write_pattern(uint8_t val);

    // -----------------------------------------------------------------
    // NextREG handlers
    // -----------------------------------------------------------------

    /// NextREG 0x15 bit 0: global sprite visibility.
    void set_sprites_visible(bool vis) { sprites_visible_ = vis; }
    bool sprites_visible() const { return sprites_visible_; }

    /// NextREG 0x19: sprite clip window X1.
    void set_clip_x1(uint8_t val) { clip_x1_ = val; }

    /// NextREG 0x1A: sprite clip window X2.
    void set_clip_x2(uint8_t val) { clip_x2_ = val; }

    /// NextREG 0x1B: sprite clip window Y1.
    void set_clip_y1(uint8_t val) { clip_y1_ = val; }

    /// NextREG 0x1C: sprite clip window Y2.
    void set_clip_y2(uint8_t val) { clip_y2_ = val; }

    /// NextREG 0x34 write: set sprite attribute slot index (alternative to
    /// port 0x303B).  bits 6:0 = sprite index, bit 7 = pattern MSB.
    void set_attr_slot(uint8_t val);

    /// NextREG 0x75-0x79: direct sprite attribute byte writes for the
    /// currently selected sprite slot.
    void write_attr_byte(uint8_t byte_idx, uint8_t val);

    /// NextREG 0x09 bit 3: sprites rendered over border (1) or clipped to
    /// display area (0).
    void set_over_border(bool val) { over_border_ = val; }

    // -----------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------

    /// Render sprites for one scanline into a 320-pixel ARGB8888 buffer.
    ///
    /// Sprites are rendered in order 0..127.  Later (higher-index) sprites
    /// appear on top of earlier ones by default.  The zero_on_top flag
    /// (NextREG 0x15 bit 5) reverses this so sprite 0 is on top.
    ///
    /// @param dst       Output buffer, 320 pixels wide (same layout as ULA
    ///                  framebuffer: 32px border + 256px display + 32px border).
    /// @param y         Scanline number in display coordinates (0-255 visible).
    /// @param palette   Palette manager for sprite colour lookups.
    void render_scanline(uint32_t* dst, int y,
                         const PaletteManager& palette) const;

    /// Render one scanline regardless of global sprites_visible_ flag.
    /// Used by the debugger video panel.
    void render_scanline_debug(uint32_t* dst, int y,
                               const PaletteManager& palette);

    /// Set whether sprite 0 is drawn on top (true) or behind (false, default).
    void set_zero_on_top(bool val) { zero_on_top_ = val; }

    /// Debug: log sprite 0 state and internal counters.
    void debug_log_sprite0() const;

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    // -----------------------------------------------------------------
    // Debug / introspection accessors
    // -----------------------------------------------------------------

    /// Read a raw attribute byte (0-4) for the given sprite index.
    /// Returns 0 if idx >= 128 or byte_idx >= 5.
    uint8_t read_attr_byte(uint8_t sprite_idx, uint8_t byte_idx) const;

    /// Decoded sprite info for debugger display.
    struct SpriteInfo {
        int      x;
        int      y;
        uint8_t  pattern;
        uint8_t  palette_offset;
        bool     visible;
        bool     x_mirror;
        bool     y_mirror;
        bool     rotate;
        bool     is_4bit;
        uint8_t  x_scale;       // 0=1x, 1=2x, 2=4x, 3=8x
        uint8_t  y_scale;
    };

    /// Get decoded sprite info. Returns zeroed struct if idx >= 128.
    SpriteInfo get_sprite_info(uint8_t idx) const;

private:
    /// Internal representation of one sprite's 5-byte attribute set.
    struct SpriteAttr {
        uint8_t byte0 = 0;  // X LSB
        uint8_t byte1 = 0;  // Y LSB
        uint8_t byte2 = 0;  // palette_offset(7:4), xmirror(3), ymirror(2), rotate(1), x_msb(0)
        uint8_t byte3 = 0;  // visible(7), extended(6), pattern(5:0)
        uint8_t byte4 = 0;  // 4bit(7), N6(6), resv(5), xscale(4:3), yscale(2:1), y_msb(0)

        // Decoded accessors
        int      x()             const { return ((byte2 & 0x01) << 8) | byte0; }
        int      y()             const;
        bool     visible()       const { return (byte3 & 0x80) != 0; }
        bool     extended()      const { return (byte3 & 0x40) != 0; }
        uint8_t  pattern_base()  const { return byte3 & 0x3F; }  // N5:N0
        uint8_t  pattern_n6()    const { return (extended() && is_4bit()) ? ((byte4 >> 6) & 1) : 0; }
        uint8_t  pattern_7bit()  const { return (pattern_base() << 1) | pattern_n6(); }
        uint8_t  palette_offset()const { return (byte2 >> 4) & 0x0F; }
        bool     x_mirror()      const { return (byte2 & 0x08) != 0; }
        bool     y_mirror()      const { return (byte2 & 0x04) != 0; }
        bool     rotate()        const { return (byte2 & 0x02) != 0; }
        bool     is_4bit()       const { return extended() && ((byte4 & 0x80) != 0); }

        // Scale factors: 0=1x, 1=2x, 2=4x, 3=8x (only valid when extended)
        uint8_t  x_scale()       const { return extended() ? ((byte4 >> 3) & 0x03) : 0; }
        uint8_t  y_scale()       const { return extended() ? ((byte4 >> 1) & 0x03) : 0; }

        // Scaled sprite dimensions
        int      width()         const { return SPRITE_SIZE << x_scale(); }
        int      height()        const { return SPRITE_SIZE << y_scale(); }

        // Anchor/relative sprite detection (VHDL sprites.vhd)
        // A sprite is relative when: extended AND byte4 bits 7:6 = "01"
        bool     is_relative()   const { return extended() && ((byte4 & 0xC0) == 0x40); }
        // Anchor type: byte4 bit 5 (only meaningful for non-relative extended sprites)
        // Type 0: relatives inherit position/pattern/palette/h4bit only
        // Type 1: additionally inherit mirror/rotate/scale with XOR
        bool     is_anchor_type1() const { return extended() && ((byte4 & 0x20) != 0); }
    };

    SpriteAttr sprites_[NUM_SPRITES];
    uint8_t    pattern_ram_[PATTERN_RAM_SZ];

    // Port 0x57 attribute upload state
    uint8_t    attr_slot_  = 0;    // current sprite index (0-127)
    uint8_t    attr_byte_  = 0;    // which attribute byte (0-4) we're writing next

    // Port 0x5B pattern upload state
    uint16_t   pattern_offset_ = 0;  // auto-incrementing, 14-bit

    // Port 0x303B pattern slot MSB (bit 7 of write to 0x303B)
    uint8_t    pattern_slot_msb_ = 0;

    // Configuration
    bool       sprites_visible_ = false;
    bool       over_border_     = false;
    bool       zero_on_top_     = false;

    // Clip window (VHDL defaults: 0x00,0xFF,0x00,0xBF)
    uint8_t    clip_x1_ = 0;
    uint8_t    clip_x2_ = 255;
    uint8_t    clip_y1_ = 0;
    uint8_t    clip_y2_ = 0xBF;   // 191 — maps to Y=223 (display bottom) in non-over-border

    // Status (sticky flags, cleared on read of port 0x303B)
    mutable bool collision_     = false;
    mutable bool max_sprites_   = false;

    // Anchor state for composite sprite chains
    struct AnchorState {
        bool     type1      = false;  // type 0 vs type 1
        bool     h4bit      = false;  // anchor's 4-bit pattern flag
        bool     visible    = false;
        int      x          = 0;      // 9-bit position
        int      y          = 0;
        uint8_t  pattern    = 0;      // 7-bit pattern (N5:N0 << 1 | N6)
        uint8_t  pal_offset = 0;      // 4-bit palette offset
        bool     rotate     = false;
        bool     x_mirror   = false;
        bool     y_mirror   = false;
        uint8_t  x_scale    = 0;      // 2-bit scale
        uint8_t  y_scale    = 0;
    };

    // Update anchor state from a non-relative sprite
    static void update_anchor(AnchorState& anchor, const SpriteAttr& spr);

    // Compute effective SpriteAttr for a relative sprite
    static SpriteAttr resolve_relative(const SpriteAttr& rel, const AnchorState& anchor);

    // Helpers
    void render_sprite_scanline(uint32_t* dst, const SpriteAttr& spr, int y,
                                const PaletteManager& palette,
                                bool* line_occupied) const;

    uint8_t read_pattern(uint16_t addr) const {
        return pattern_ram_[addr & (PATTERN_RAM_SZ - 1)];
    }
};
