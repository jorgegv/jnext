#include "video/tilemap.h"

#include "memory/ram.h"
#include "video/palette.h"
#include "core/saveable.h"

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void Tilemap::reset()
{
    control_raw_   = 0;
    enabled_       = false;
    mode_80col_    = false;
    text_mode_     = false;
    force_attr_    = false;
    mode_512_      = false;
    ula_on_top_    = false;
    default_attr_  = 0;
    // VHDL defaults: map base = 0x2C (offset 44 → 0x6C00), tile defs = 0x0C (offset 12 → 0x4C00)
    map_base_raw_  = 0x2C;
    def_base_raw_  = 0x0C;
    map_base_addr_ = decode_base_addr(0x2C);
    def_base_addr_ = decode_base_addr(0x0C);
    scroll_x_      = 0;
    scroll_y_      = 0;
}

// ---------------------------------------------------------------------------
// NextREG 0x6B — Tilemap control
// ---------------------------------------------------------------------------

void Tilemap::set_control(uint8_t val)
{
    control_raw_ = val;
    enabled_     = (val & 0x80) != 0;
    mode_80col_  = (val & 0x40) != 0;
    text_mode_   = (val & 0x20) != 0;
    force_attr_  = (val & 0x10) != 0;
    mode_512_    = (val & 0x02) != 0;
    ula_on_top_  = (val & 0x01) != 0;
}

// ---------------------------------------------------------------------------
// Base address decoding
// ---------------------------------------------------------------------------
//
// NextREG 0x6E / 0x6F encode a physical address within two 16K banks
// (bank 5 = 0x0A000..0x0DFFF in 8K pages, bank 7 = 0x0E000..0x11FFF).
//
// From VHDL (zxnext.vhd lines 5468-5469, tilemap.vhd line 402-403):
//
//   nr_6e_tilemap_base_7 <= nr_wr_dat(7);         -- bank select
//   nr_6e_tilemap_base   <= nr_wr_dat(5 downto 0); -- 6-bit offset
//
// The tilemap module receives a 7-bit input: { bank_select, offset[5:0] }.
// Bit 6 of the register is unused.  The address within a 16K bank is:
//   addr[13:8] = sub[13:8] + offset[5:0]
//   addr[7:0]  = sub[7:0]
// Each offset unit = 2^8 = 256 bytes.  Physical addresses:
//   Bank 5 base = 5 * 16384 = 0x14000
//   Bank 7 base = 7 * 16384 = 0x1C000
//
// VHDL defaults: map base = 44 (0x6C00), tile defs = 12 (0x4C00).

uint32_t Tilemap::decode_base_addr(uint8_t reg_val)
{
    bool bank7 = (reg_val & 0x80) != 0;   // bit 7 = bank select
    uint8_t offset = reg_val & 0x3F;       // bits 5:0 = 256-byte offset

    uint32_t bank_base = bank7 ? (7u * 16384u) : (5u * 16384u);
    return bank_base + static_cast<uint32_t>(offset) * 256u;
}

void Tilemap::set_map_base(uint8_t val)
{
    map_base_raw_  = val;
    map_base_addr_ = decode_base_addr(val);
}

void Tilemap::set_def_base(uint8_t val)
{
    def_base_raw_  = val;
    def_base_addr_ = decode_base_addr(val);
}

// ---------------------------------------------------------------------------
// Render one scanline
// ---------------------------------------------------------------------------
//
// Tilemap layout (VHDL-derived):
//
// 40-column mode:
//   - 40 tiles per row, each 8 pixels wide = 320 display pixels
//   - Map entry = 2 bytes when flags present: [tile_index, attribute]
//   - Map entry = 1 byte when flags stripped: [tile_index]
//   - Map memory size: 40*32*2 = 2560 bytes (with flags), 40*32 = 1280 (stripped)
//   - Total tilemap area is 40*8 = 320 pixels wide, 32*8 = 256 pixels tall
//
// 80-column mode:
//   - 80 tiles per row, each 8 pixels wide = 640 tilemap pixels
//   - Displayed 2:1 onto 320 physical pixels (each screen pixel = 2 tilemap pixels)
//   - Map entry same format as 40-col
//   - Map memory size: 80*32*2 = 5120 bytes (with flags), 80*32 = 2560 (stripped)
//
// Tile definition (pattern) memory:
//   - Each tile is 8x8 pixels at 4bpp = 32 bytes per tile
//   - 4 bytes per row, 2 pixels per byte (high nibble = left pixel)
//   - In text mode, each tile is 8x8 monochrome = 8 bytes per tile (1 byte per row)
//
// Attribute byte format:
//   bit 7:4 = palette offset (added to 4-bit pixel value to form 8-bit palette index)
//   bit 3   = X mirror
//   bit 2   = Y mirror
//   bit 1   = Rotate (swap X/Y after mirror)
//   bit 0   = ULA-over-tilemap (per-tile; in 512-tile mode this is tile bit 8 instead)
//
// Text mode attribute:
//   bit 7:1 = palette offset (7 bits, giving 128 possible offsets)
//   bit 0   = ULA-over-tilemap (in 512-tile mode: tile bit 8)
//
// Scroll wrapping:
//   X scroll wraps at 320 (40-col) or 640 (80-col) pixel boundary.
//   Y scroll wraps at 256 pixels (32 tiles * 8).

void Tilemap::render_scanline_debug(uint32_t* dst, bool* ula_over_flags, int y,
                                    const Ram& ram,
                                    const PaletteManager& palette)
{
    const bool saved = enabled_;
    enabled_ = true;
    render_scanline(dst, ula_over_flags, y, ram, palette);
    enabled_ = saved;
}

void Tilemap::render_scanline(uint32_t* dst, bool* ula_over_flags, int y,
                              const Ram& ram,
                              const PaletteManager& palette) const
{
    if (!enabled_ || y < 0 || y >= 256)
        return;

    const uint8_t transp_idx = palette.tilemap_transparency();

    // Compute the absolute Y position with scroll applied (wraps at 256).
    const int abs_y = (y + scroll_y_) & 0xFF;
    const int tile_row = abs_y >> 3;       // which tile row (0-31)
    const int pixel_y  = abs_y & 7;        // pixel row within tile (0-7)

    // Number of tiles per row.
    const int tiles_per_row = mode_80col_ ? 80 : 40;

    // 80-col mode is 640x256 (80 tiles × 8 pixels), displayed in 320 physical
    // pixels. Each screen pixel maps to 2 tilemap pixels; we show the left one
    // (even column). X scroll wraps at 640 in 80-col, 320 in 40-col.
    const int wrap_x = mode_80col_ ? 640 : 320;

    // Map memory layout:
    // - With flags: tile_row * tiles_per_row * 2 + tile_col * 2
    //   byte 0 = tile index, byte 1 = attribute
    // - Stripped (force_attr): tile_row * tiles_per_row + tile_col
    //   byte 0 = tile index
    //
    // From VHDL: tm_mem_addr_tile_sub_sub =
    //   (tm_abs_y(11:3) + ("00000" & tm_abs_x(9:6))) & tm_abs_x(5:3)
    // tm_abs_y(11:3) = tile_row * tiles_per_row (computed via multiply in VHDL)
    // tm_abs_x(9:6) & tm_abs_x(5:3) gives the tile column index.
    //
    // For non-stripped: address = (row_offset + tile_col) * 2 + (0 for index, 1 for attr)
    // For stripped: address = row_offset + tile_col

    // Bytes per tile entry in the map.
    const int map_entry_size = force_attr_ ? 1 : 2;

    // Row offset in map memory (tile entries).
    const uint32_t map_row_offset = static_cast<uint32_t>(tile_row) * tiles_per_row;

    // The output framebuffer has 32px left border + 256px display + 32px right border = 320px total.
    // Tilemap covers the full 320px display width in the framebuffer.
    // Wait -- looking at the layer2.cpp, it offsets by 32 for 256px content. But tilemap is 320px wide.
    // Actually the framebuffer is 320 pixels, with indices 0-319. The ULA display area is at
    // offset 32 (pixels 32-287). But the tilemap covers all 320 pixels (it's a 320px-wide layer).
    // Let me check the VHDL: pixel_en_s checks hcounter_i < 320 && vcounter_i(8) = '0'.
    // So tilemap is active for the full 320-pixel display width, overlapping the border area.

    for (int screen_x = 0; screen_x < 320; ++screen_x) {
        // In 80-col mode, 640 tilemap pixels map to 320 screen pixels.
        // Each screen pixel shows the left (even) tilemap pixel.
        int tilemap_x = mode_80col_ ? (screen_x * 2) : screen_x;
        int abs_x = (tilemap_x + scroll_x_) % wrap_x;

        // Tile column and pixel within tile (always 8 pixels per tile).
        int tile_col = abs_x / 8;
        int pixel_x  = abs_x % 8;

        // Read tile index from map memory.
        uint32_t map_addr = map_base_addr_ + (map_row_offset + tile_col) * map_entry_size;
        uint8_t tile_index = ram.read(map_addr);

        // Read or apply attribute.
        uint8_t attr;
        if (force_attr_) {
            attr = default_attr_;
        } else {
            attr = ram.read(map_addr + 1);
        }

        // Decode attribute.
        uint8_t pal_offset;
        bool x_mirror, y_mirror, rotate;
        bool tile_ula_over;

        if (text_mode_) {
            // Text mode: bits 7:1 = palette offset (7 bits), bit 0 = ULA-over / tile bit 8.
            pal_offset = (attr >> 1) & 0x7F;
            x_mirror   = false;
            y_mirror   = false;
            rotate     = false;
            tile_ula_over = (attr & 0x01) != 0;
        } else {
            // Standard mode: bits 7:4 = palette offset, bit 3 = X mirror,
            // bit 2 = Y mirror, bit 1 = rotate, bit 0 = ULA-over / tile bit 8.
            pal_offset = (attr >> 4) & 0x0F;
            x_mirror   = (attr & 0x08) != 0;
            y_mirror   = (attr & 0x04) != 0;
            rotate     = (attr & 0x02) != 0;
            tile_ula_over = (attr & 0x01) != 0;
        }

        // In 512-tile mode, attr bit 0 is tile index bit 8 instead of ULA-over.
        uint16_t full_tile_index = tile_index;
        if (mode_512_) {
            full_tile_index |= (attr & 0x01) ? 0x100 : 0;
            // In 512-tile mode, per-tile ULA-over is not available.
            tile_ula_over = false;
        }

        // Determine the ULA-over flag for this pixel.
        // From VHDL: pixel_below = (tm_tilemap_1(0) or mode_512_q) and not tm_on_top_q
        // tile_ula_over is attr bit 0; in 512 mode it's always set (tile bit 8),
        // but tm_on_top_q overrides.
        bool pixel_below;
        if (ula_on_top_) {
            // Global ULA-on-top: tilemap is always below ULA.
            pixel_below = false;
        } else {
            // Per-tile: below if ULA-over bit or 512-mode.
            pixel_below = tile_ula_over || mode_512_;
        }

        // Apply transforms to get the effective pixel coordinates within the tile.
        int eff_px = pixel_x;  // 0-7
        int eff_py = pixel_y;  // 0-7

        if (!text_mode_) {
            // VHDL transform chain:
            // effective_x_mirror = x_mirror XOR rotate (rotation inverts x mirror)
            // effective_x = x_mirror_effective ? (7 - abs_x[2:0]) : abs_x[2:0]
            // effective_y = y_mirror ? (7 - abs_y[2:0]) : abs_y[2:0]
            // transformed_x = rotate ? effective_y : effective_x
            // transformed_y = rotate ? effective_x : effective_y

            bool eff_x_mirror = x_mirror ^ rotate;
            if (eff_x_mirror)
                eff_px = 7 - eff_px;
            if (y_mirror)
                eff_py = 7 - eff_py;
            if (rotate) {
                int tmp = eff_px;
                eff_px = eff_py;
                eff_py = tmp;
            }
        }

        // Fetch the pixel value from tile definition memory.
        uint8_t pixel_4bit;

        if (text_mode_) {
            // Text mode: 1bpp, 8 bytes per tile (one byte per row).
            // Each byte is a bitmask for 8 pixels; we shift to get the desired bit.
            uint32_t def_addr = def_base_addr_
                + static_cast<uint32_t>(full_tile_index) * 8
                + eff_py;
            uint8_t row_byte = ram.read(def_addr);
            // VHDL: shift_left by abs_x(2:0), then take bit 7.
            // For 80-col, pixel_x is 0-3; for 40-col, 0-7.
            // The original abs_x(2:0) in VHDL is the raw pixel position.
            pixel_4bit = (row_byte >> (7 - pixel_x)) & 0x01;
        } else {
            // Standard mode: 4bpp, 32 bytes per tile (4 bytes per row).
            // Layout: byte = tile_row[eff_py], column = eff_px / 2
            // High nibble = even pixel (eff_px & 1 == 0), low nibble = odd pixel.
            //
            // VHDL address: (full_tile_index & transformed_y & transformed_x[2:1])
            // That's: tile_index * 32 + eff_py * 4 + (eff_px >> 1)
            uint32_t def_addr = def_base_addr_
                + static_cast<uint32_t>(full_tile_index) * 32
                + static_cast<uint32_t>(eff_py) * 4
                + (eff_px >> 1);
            uint8_t pattern_byte = ram.read(def_addr);

            // VHDL: high nibble when transformed_x(0) == 0, low nibble otherwise.
            if ((eff_px & 1) == 0)
                pixel_4bit = (pattern_byte >> 4) & 0x0F;
            else
                pixel_4bit = pattern_byte & 0x0F;
        }

        // Check transparency.
        if (!text_mode_ && pixel_4bit == transp_idx)
            continue;

        // In text mode, pixel_4bit is 0 or 1. A value of 0 means transparent
        // (no pixel drawn), value 1 means foreground.
        if (text_mode_ && pixel_4bit == 0)
            continue;

        // Build the full 8-bit palette index.
        // Standard: upper 4 bits from attr palette offset, lower 4 from pixel.
        // Text mode: upper 7 bits from attr (bits 7:1), lower 1 bit from pixel.
        uint8_t colour_idx;
        if (text_mode_) {
            colour_idx = static_cast<uint8_t>((pal_offset << 1) | (pixel_4bit & 0x01));
        } else {
            colour_idx = static_cast<uint8_t>((pal_offset << 4) | (pixel_4bit & 0x0F));
        }

        dst[screen_x] = palette.tilemap_colour(colour_idx);

        if (ula_over_flags)
            ula_over_flags[screen_x] = pixel_below;
    }
}

void Tilemap::save_state(StateWriter& w) const
{
    w.write_u8(control_raw_);
    w.write_bool(enabled_);
    w.write_bool(mode_80col_);
    w.write_bool(text_mode_);
    w.write_bool(force_attr_);
    w.write_bool(mode_512_);
    w.write_bool(ula_on_top_);
    w.write_u8(default_attr_);
    w.write_u8(map_base_raw_);
    w.write_u8(def_base_raw_);
    w.write_u32(map_base_addr_);
    w.write_u32(def_base_addr_);
    w.write_u16(scroll_x_);
    w.write_u8(scroll_y_);
    w.write_u8(clip_x1_); w.write_u8(clip_x2_);
    w.write_u8(clip_y1_); w.write_u8(clip_y2_);
}

void Tilemap::load_state(StateReader& r)
{
    control_raw_ = r.read_u8();
    enabled_ = r.read_bool();
    mode_80col_ = r.read_bool();
    text_mode_ = r.read_bool();
    force_attr_ = r.read_bool();
    mode_512_ = r.read_bool();
    ula_on_top_ = r.read_bool();
    default_attr_ = r.read_u8();
    map_base_raw_ = r.read_u8();
    def_base_raw_ = r.read_u8();
    map_base_addr_ = r.read_u32();
    def_base_addr_ = r.read_u32();
    scroll_x_ = r.read_u16();
    scroll_y_ = r.read_u8();
    clip_x1_ = r.read_u8(); clip_x2_ = r.read_u8();
    clip_y1_ = r.read_u8(); clip_y2_ = r.read_u8();
}
