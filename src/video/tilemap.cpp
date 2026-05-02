#include "video/tilemap.h"

#include "core/log.h"
#include "core/saveable.h"
#include "memory/ram.h"
#include "video/palette.h"

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
    palette_sel_   = false;   // NR 0x6B bit 4 defaults to 0
    default_attr_  = 0;
    // VHDL defaults: map base = 0x2C (offset 44 → 0x6C00), tile defs = 0x0C (offset 12 → 0x4C00)
    map_base_raw_  = 0x2C;
    def_base_raw_  = 0x0C;
    map_base_addr_ = decode_base_addr(0x2C);
    def_base_addr_ = decode_base_addr(0x0C);
    scroll_x_      = 0;
    scroll_y_      = 0;

    // Per-scanline NR 0x6B change log cleared. Baseline reset to current
    // (zero) state; start_frame_nr6b() will re-snapshot from the live
    // values at the next frame boundary.
    nr6b_change_count_     = 0;
    nr6b_render_cursor_    = 0;
    nr6b_current_line_     = 0;
    nr6b_overflow_warned_  = false;
    baseline_control_raw_  = 0;
    baseline_enabled_      = false;
    baseline_mode_80col_   = false;
    baseline_text_mode_    = false;
    baseline_force_attr_   = false;
    baseline_mode_512_     = false;
    baseline_ula_on_top_   = false;
}

// ---------------------------------------------------------------------------
// NextREG 0x6B — Tilemap control
// ---------------------------------------------------------------------------

void Tilemap::set_control(uint8_t val)
{
    // VHDL tilemap.vhd:189-195 control_i mapping:
    //   bit 7 = enable (directly in nextreg, not in control_i(6:0))
    //   bit 6 = mode (40/80 col)
    //   bit 5 = strip_flags (eliminate tilemap attribute byte)
    //   bit 4 = nr_6b_tm_palette_select (tilemap palette 0/1 for render)
    //   bit 3 = textmode (extend palette offset to 7 bits)
    //   bit 1 = 512-tile mode
    //   bit 0 = tm_on_top (tilemap always above ULA)
    control_raw_ = val;
    enabled_     = (val & 0x80) != 0;
    mode_80col_  = (val & 0x40) != 0;
    force_attr_  = (val & 0x20) != 0;   // bit 5 = strip_flags
    palette_sel_ = (val & 0x10) != 0;   // bit 4 = VHDL nr_6b_tm_palette_select
    text_mode_   = (val & 0x08) != 0;   // bit 3 = textmode
    mode_512_    = (val & 0x02) != 0;
    ula_on_top_  = (val & 0x01) != 0;

    // Log the 5 Tilemap-owned bits for per-scanline replay (G06).
    log_nr6b_change();
}

// ---------------------------------------------------------------------------
// Per-scanline NR 0x6B change log (G06)
// ---------------------------------------------------------------------------
//
// Mirrors layer2's NR 0x70 / scroll / clip / bank / enable change-logs.
// The 5 bits owned by Tilemap (b7 enable, b6 80-col, b5 force_attr,
// b3 textmode, b1 512-tile, b0 tm_on_top) are packed into one control
// byte per snapshot. Bit 4 (palette select) is owned by the Ula class
// (UL-C's palsel6b log), so it is intentionally cleared in the snapshot
// — the replay code in this class never touches palette_sel_.

void Tilemap::log_nr6b_change()
{
    if (nr6b_change_count_ >= MAX_NR6B_CHANGES_PER_FRAME) {
        if (!nr6b_overflow_warned_) {
            Log::video()->warn(
                "Tilemap: NR 0x6B change-log full at line {} (cap {} per "
                "frame); further NR 0x6B writes this frame will not be "
                "per-scanline.",
                nr6b_current_line_, MAX_NR6B_CHANGES_PER_FRAME);
            nr6b_overflow_warned_ = true;
        }
        return;
    }
    // Reconstruct the NR 0x6B byte from the 5 owned fields.
    // Bit 4 (palette select) is masked off — it is replayed by the
    // Ula::palsel6b log, not by us.
    uint8_t control_byte =
          (enabled_     ? 0x80 : 0)
        | (mode_80col_  ? 0x40 : 0)
        | (force_attr_  ? 0x20 : 0)
        | (text_mode_   ? 0x08 : 0)
        | (mode_512_    ? 0x02 : 0)
        | (ula_on_top_  ? 0x01 : 0);
    nr6b_change_log_[nr6b_change_count_++] = Nr6bChange{
        nr6b_current_line_, control_byte,
    };
}

void Tilemap::replay_control_byte(uint8_t val)
{
    // Mirror set_control without recursing into log_nr6b_change.
    // Bit 4 is intentionally not touched — palette_sel_ is owned by the
    // Ula class via its palsel6b change-log (UL-C path).
    enabled_    = (val & 0x80) != 0;
    mode_80col_ = (val & 0x40) != 0;
    force_attr_ = (val & 0x20) != 0;
    text_mode_  = (val & 0x08) != 0;
    mode_512_   = (val & 0x02) != 0;
    ula_on_top_ = (val & 0x01) != 0;
    // control_raw_ is mostly diagnostic; keep it in sync but preserve any
    // bit 4 state the live setter may have left there.
    control_raw_ = (control_raw_ & 0x10) | (val & ~uint8_t{0x10});
}

void Tilemap::start_frame_nr6b()
{
    baseline_control_raw_  = control_raw_;
    baseline_enabled_      = enabled_;
    baseline_mode_80col_   = mode_80col_;
    baseline_text_mode_    = text_mode_;
    baseline_force_attr_   = force_attr_;
    baseline_mode_512_     = mode_512_;
    baseline_ula_on_top_   = ula_on_top_;
    nr6b_change_count_     = 0;
    nr6b_render_cursor_    = 0;
    nr6b_current_line_     = 0;
    nr6b_overflow_warned_  = false;
}

void Tilemap::rewind_nr6b_to_baseline()
{
    control_raw_        = baseline_control_raw_;
    enabled_            = baseline_enabled_;
    mode_80col_         = baseline_mode_80col_;
    text_mode_          = baseline_text_mode_;
    force_attr_         = baseline_force_attr_;
    mode_512_           = baseline_mode_512_;
    ula_on_top_         = baseline_ula_on_top_;
    nr6b_render_cursor_ = 0;
}

void Tilemap::apply_nr6b_changes_for_line(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);
    while (nr6b_render_cursor_ < nr6b_change_count_
        && nr6b_change_log_[nr6b_render_cursor_].line == lt) {
        const auto& c = nr6b_change_log_[nr6b_render_cursor_++];
        replay_control_byte(c.control_byte);
    }
}

void Tilemap::flush_remaining_nr6b_changes()
{
    // Drain any log entries that the per-line render loop did not reach
    // — typically vblank writes (line >= FB_HEIGHT). Without this, the
    // live state at end-of-render would equal the LAST visible-line
    // replay value, and the next frame's start_frame_nr6b() would
    // snapshot a stale baseline (e.g. demo writes NR 0x6B during
    // waitForScanline(255) → sc_update never propagates to subsequent
    // frames). Replays in log order so the final state matches the
    // post-emulation live state.
    while (nr6b_render_cursor_ < nr6b_change_count_) {
        const auto& c = nr6b_change_log_[nr6b_render_cursor_++];
        replay_control_byte(c.control_byte);
    }
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
//   - 40 tiles per row, each 8 source pixels wide = 320 source pixels
//   - Each source pixel covers 2 framebuffer cells (7 MHz tap; VHDL
//     tilemap.vhd:228 hcount(10:2)) → 640 framebuffer cells per scanline.
//   - Map entry = 2 bytes when flags present: [tile_index, attribute]
//   - Map entry = 1 byte when flags stripped: [tile_index]
//   - Map memory size: 40*32*2 = 2560 bytes (with flags), 40*32 = 1280 (stripped)
//
// 80-column mode:
//   - 80 tiles per row, each 8 source pixels wide = 640 source pixels
//   - 1:1 mapping onto 640 framebuffer cells (14 MHz tap; VHDL
//     tilemap.vhd:228 hcount(10:1)).
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
                                    const PaletteManager& palette,
                                    bool* textmode_flags)
{
    const bool saved = enabled_;
    enabled_ = true;
    // Use current register values (not per-line snapshots) so the debugger
    // shows the tilemap with the live scroll position.
    if (y >= 0 && y < kSnapshotLines) {
        scroll_x_per_line_[y] = scroll_x_;
        scroll_y_per_line_[y] = scroll_y_;
    }
    render_scanline(dst, ula_over_flags, y, ram, palette, textmode_flags);
    enabled_ = saved;
}

void Tilemap::render_scanline(uint32_t* dst, bool* ula_over_flags, int y,
                              const Ram& ram,
                              const PaletteManager& palette,
                              bool* textmode_flags) const
{
    if (!enabled_ || y < 0 || y >= 256)
        return;

    // Clip window — VHDL tilemap.vhd:415-424 pixel_en_s gates output
    // against [xsv, xev] horizontal and [ysv, yev] vertical, where
    //   xsv <= clip_x1 & '0'   (= clip_x1 * 2)
    //   xev <= clip_x2 & '1'   (= clip_x2 * 2 + 1)
    // and the comparator runs in the 9-bit hcounter domain (hcounter <
    // 320 visible).  Y coords are direct.
    //
    // G104: output is now 640 cells wide regardless of col-mode.
    const int clip_out_width = 640;

    // Y-clip short-circuit — entire scanline outside clip rectangle.
    if (y < clip_y1_ || y > clip_y2_) {
        for (int i = 0; i < clip_out_width; ++i)
            dst[i] = 0u;  // transparent (ARGB8888 zero-alpha)
        return;
    }

    // X-clip bounds in the VHDL hcounter (320-grid) domain — int so the
    // upper bound 0x13F = 319 is safe (no 8-bit wrap).
    const int clip_xlo_320 = static_cast<int>(clip_x1_) * 2;
    const int clip_xhi_320 = static_cast<int>(clip_x2_) * 2 + 1;

    // Shift mapping screen_x (0..639) into the index that is compared
    // against the 320-grid clip range [clip_x1*2, clip_x2*2+1].  VHDL
    // tilemap.vhd:415-424 fires the clip gate at 7 MHz (hcounter
    // granularity).  In our 14 MHz / 640-cell framebuffer:
    //   80-col (VHDL tm_mode='1'): hcount(10:1) = 14 MHz source pixels
    //     → 1 source pixel per output cell, 2 source pixels per
    //     hcounter step.  Map screen_x → hcounter via `>> 1`.
    //   40-col (VHDL tm_mode='0'): hcount(10:2) = 7 MHz source pixels
    //     held into the 14 MHz output → 1 source pixel per 2 output
    //     cells, 1 source pixel per hcounter step.  In this code path
    //     each output cell receives an independent clip decision keyed
    //     directly on screen_x (i.e. shift=0); pairs of output cells
    //     fall on the same source-pixel anyway, so clip ranges that
    //     are 2-cell-aligned reproduce hcounter-granular gating, while
    //     odd-aligned ranges split a doubled source pixel — the paired
    //     downstream code in `tilemap_x = screen_x >> 1` makes the two
    //     halves still reference the same RAM byte, only one is masked.
    //     This matches the prompt's locked decision; if VHDL-faithful
    //     hcounter-granular gating is required (i.e. force shift=1 for
    //     both modes), TM-112-style 2-cell tests need to assert
    //     dst[0x40..0x43] opaque instead of dst[0x20..0x21].
    const int clip_x_shift = mode_80col_ ? 1 : 0;

    const uint8_t transp_idx = palette.tilemap_transparency();

    // Use per-scanline scroll values (captured during the frame loop).
    const uint16_t line_scroll_x = (y >= 0 && y < kSnapshotLines) ? scroll_x_per_line_[y] : scroll_x_;
    const uint8_t  line_scroll_y = (y >= 0 && y < kSnapshotLines) ? scroll_y_per_line_[y] : scroll_y_;



    // Compute the absolute Y position with scroll applied (wraps at 256).
    const int abs_y = (y + line_scroll_y) & 0xFF;
    const int tile_row = abs_y >> 3;       // which tile row (0-31)
    const int pixel_y  = abs_y & 7;        // pixel row within tile (0-7)

    // Number of tiles per row.
    const int tiles_per_row = mode_80col_ ? 80 : 40;

    // X scroll wrap: 320 in 40-col (40 tiles × 8 px), 640 in 80-col
    // (80 tiles × 8 px).
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

    // G104: framebuffer is now canonical 640 (64 left border + 512 display
    // + 64 right border).  Tilemap covers the full 640 width — VHDL
    // tilemap.vhd:424 gates pixel_en_s purely on hcounter_i < 320 and
    // vcounter_i(8)='0', i.e. tilemap overlays the border area too.

    // Output width is always 640 — no col-mode branch.
    const int out_width = 640;

    for (int screen_x = 0; screen_x < out_width; ++screen_x) {
        // Tile-fetch source-pixel index.
        //   80-col (VHDL tm_mode='1', 14 MHz tap): native 1:1 — each
        //     output cell reads its own tilemap pixel (640 unique).
        //     tilemap_x = screen_x  (range 0..639).
        //   40-col (VHDL tm_mode='0', 7 MHz tap): each tilemap pixel
        //     covers two adjacent output cells.  Pairs (2k, 2k+1) read
        //     the same source.  tilemap_x = screen_x >> 1 (range 0..319).
        const int tilemap_x = mode_80col_ ? screen_x : (screen_x >> 1);

        // Per-pixel X-clip — blank to transparent when the source-pixel
        // index (mapped to the 320-grid clip domain via clip_x_shift)
        // falls outside [clip_x1*2, clip_x2*2+1].  Because clip_x_shift
        // also collapses the 80-col 14 MHz pacing into the same 320-grid
        // hcounter coord, both modes share one comparator.  VHDL
        // tilemap.vhd:415-424.
        const int pixel_x_320 = screen_x >> clip_x_shift;
        if (pixel_x_320 < clip_xlo_320 || pixel_x_320 > clip_xhi_320) {
            dst[screen_x] = 0u;  // transparent — skip rest of per-pixel work
            continue;
        }

        int abs_x = (tilemap_x + line_scroll_x) % wrap_x;

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

        // Per-pixel textmode flag emission (G101).  VHDL tilemap.vhd:426,
        // 437, 443: `pixel_textmode_s` is latched alongside `pixel_en_f`
        // and `pixel_below` and surfaced as `pixel_textmode_o`.  In our
        // line-batched renderer the textmode flag is sourced from the
        // global `text_mode_` control bit which spans the whole
        // scanline; only emitted (non-transparent) pixels are tagged so
        // downstream compositor logic mirrors VHDL `pixel_en_f`-gated
        // pipeline state.
        if (textmode_flags)
            textmode_flags[screen_x] = text_mode_;
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
    w.write_bool(palette_sel_);   // NR 0x6B bit 4
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
    palette_sel_ = r.read_bool();   // NR 0x6B bit 4
}
