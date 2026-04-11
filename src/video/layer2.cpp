#include "video/layer2.h"
#include "video/palette.h"
#include "memory/ram.h"
#include "core/saveable.h"

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void Layer2::reset()
{
    active_bank_    = 8;
    shadow_bank_    = 11;
    scroll_x_       = 0;
    scroll_y_       = 0;
    palette_offset_ = 0;
    resolution_     = 0;
    enabled_        = false;
    clip_x1_        = 0;
    clip_x2_        = 255;
    clip_y1_        = 0;
    clip_y2_        = 255;
}

// ---------------------------------------------------------------------------
// NextREG 0x70 — Layer 2 control
// ---------------------------------------------------------------------------

void Layer2::set_control(uint8_t val)
{
    resolution_     = (val >> 4) & 0x03;
    palette_offset_ = val & 0x0F;
}

// ---------------------------------------------------------------------------
// Render one scanline
// ---------------------------------------------------------------------------
//
// VHDL reference: layer2.vhd
//
// Memory address generation:
//   256x192:  addr = y[7:0] & x[7:0]         (row-major)
//   320x256:  addr = x[8:0] & y[7:0]         (column-major, 8bpp)
//   640x256:  addr = x[8:0] & y[7:0]         (column-major, 4bpp, 2px/byte)
//
// Bank mapping:
//   layer2_bank_eff = ((0 & bank[6:4]) + 1) & bank[3:0]
//   layer2_addr_eff = (bank_eff + addr[16:14]) & addr[13:0]
//   Physical RAM offset = layer2_addr_eff * 2 bytes (SRAM words)
//   But in our emulator, RAM is byte-addressed from bank base.

static inline uint32_t compute_ram_addr(uint8_t active_bank, uint32_t l2_addr)
{
    // VHDL: layer2_bank_eff = (('0' & bank(6:4)) + 1) & bank(3:0)
    // This converts a 16K bank number into an 8-bit SRAM bank number.
    // In our emulator, we just use 16K bank * 16384 + offset.
    int bank_16k = active_bank;
    int sub_bank = static_cast<int>(l2_addr >> 14);      // which 16K chunk (0-4)
    int offset   = static_cast<int>(l2_addr & 0x3FFF);   // offset within 16K
    return static_cast<uint32_t>((bank_16k + sub_bank) * 16384 + offset);
}

void Layer2::render_scanline_debug(uint32_t* dst, int row, const Ram& ram,
                                   const PaletteManager& palette, uint8_t bank)
{
    const bool saved_enabled = enabled_;
    const uint8_t saved_bank = active_bank_;
    enabled_      = true;
    active_bank_  = bank;
    render_scanline(dst, row, ram, palette);
    enabled_      = saved_enabled;
    active_bank_  = saved_bank;
}

void Layer2::render_scanline(uint32_t* dst, int row, const Ram& ram,
                             const PaletteManager& palette) const
{
    if (!enabled_)
        return;

    // VHDL transparency: compares the 8-bit RRRGGGBB palette output
    // (NOT the raw pixel index) against the global transparency colour
    // (NextREG 0x14).  See zxnext.vhd line 7121.
    uint8_t transp_rgb = palette.global_transparency();

    if (resolution_ == 0) {
        // ---------------------------------------------------------------
        // 256x192 @ 8bpp (row-major)
        // ---------------------------------------------------------------
        // row is a framebuffer row (0-255). Display area is rows 32-223.
        static constexpr int DISP_Y = 32;
        static constexpr int DISP_X = 32;
        int y = row - DISP_Y;
        if (y < 0 || y >= 192)
            return;

        // Y scroll wraps at 192.
        int src_y = (y + scroll_y_) % 192;

        // Clip Y check (256x192 mode: clip_y range is 0-191).
        if (src_y < clip_y1_ || src_y > clip_y2_)
            return;

        // Bank and row offset.
        int third = src_y / 64;
        int row_in_third = src_y % 64;
        uint32_t l2_addr_base = static_cast<uint32_t>(src_y) * 256;

        for (int x = 0; x < 256; ++x) {
            int src_x = (x + (scroll_x_ & 0xFF)) & 0xFF;

            // Clip X check.
            if (src_x < clip_x1_ || src_x > clip_x2_)
                continue;

            uint32_t l2_addr = l2_addr_base + src_x;
            uint32_t ram_addr = compute_ram_addr(active_bank_, l2_addr);
            uint8_t pixel = ram.read(ram_addr);

            uint8_t colour_idx = static_cast<uint8_t>(
                ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

            // Transparency: compare palette RRRGGGBB against global transparent colour.
            if (palette.layer2_rgb8(colour_idx) == transp_rgb)
                continue;

            dst[DISP_X + x] = palette.layer2_colour(colour_idx);
        }
    }
    else if (resolution_ == 1) {
        // ---------------------------------------------------------------
        // 320x256 @ 8bpp (column-major: addr = x * 256 + y)
        // ---------------------------------------------------------------
        if (row < 0 || row >= 256)
            return;

        // Y scroll wraps at 256 (natural 8-bit wrap).
        uint8_t src_y = static_cast<uint8_t>(row + scroll_y_);

        // Clip Y (wide mode: 0-255).
        if (src_y < clip_y1_ || src_y > clip_y2_)
            return;

        // VHDL clip for wide mode: clip_x1 & '0', clip_x2 & '1'
        // So clip register value is doubled: clip_x1*2 .. clip_x2*2+1
        uint16_t clip_x1_eff = static_cast<uint16_t>(clip_x1_) << 1;
        uint16_t clip_x2_eff = (static_cast<uint16_t>(clip_x2_) << 1) | 1;

        for (int x = 0; x < 320; ++x) {
            // X scroll with wrap at 320.
            int src_x_pre = x + (scroll_x_ & 0x1FF);
            int src_x = (src_x_pre >= 320) ? (src_x_pre - 320) : src_x_pre;

            // Clip X.
            if (src_x < clip_x1_eff || src_x > clip_x2_eff)
                continue;

            // Column-major: addr = x * 256 + y (17-bit).
            uint32_t l2_addr = static_cast<uint32_t>(src_x) * 256 + src_y;
            uint32_t ram_addr = compute_ram_addr(active_bank_, l2_addr);
            uint8_t pixel = ram.read(ram_addr);

            uint8_t colour_idx = static_cast<uint8_t>(
                ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

            if (palette.layer2_rgb8(colour_idx) == transp_rgb)
                continue;

            dst[x] = palette.layer2_colour(colour_idx);
        }
    }
    else {
        // ---------------------------------------------------------------
        // 640x256 @ 4bpp (column-major: addr = x * 256 + y, 2 px/byte)
        // ---------------------------------------------------------------
        // resolution_ == 2 or 3 both select this mode (VHDL: resolution(1)='1').
        if (row < 0 || row >= 256)
            return;

        uint8_t src_y = static_cast<uint8_t>(row + scroll_y_);

        if (src_y < clip_y1_ || src_y > clip_y2_)
            return;

        uint16_t clip_x1_eff = static_cast<uint16_t>(clip_x1_) << 1;
        uint16_t clip_x2_eff = (static_cast<uint16_t>(clip_x2_) << 1) | 1;

        // Each memory address holds 2 horizontal pixels.
        // We iterate over 320 memory columns, each producing 2 pixels at
        // positions 2*col and 2*col+1.  Our framebuffer is 320 pixels wide,
        // so we render the left pixel of each pair (half horizontal resolution).
        for (int col = 0; col < 320; ++col) {
            int src_col_pre = col + (scroll_x_ & 0x1FF);
            int src_col = (src_col_pre >= 320) ? (src_col_pre - 320) : src_col_pre;

            // Clip X against the 640-pixel coordinate space.
            int pixel_x = col;  // framebuffer position (0-319)
            // In 640x256, clip coords are in the 320 address space doubled.
            if (src_col < (clip_x1_eff >> 1) || src_col > (clip_x2_eff >> 1))
                continue;

            uint32_t l2_addr = static_cast<uint32_t>(src_col) * 256 + src_y;
            uint32_t ram_addr = compute_ram_addr(active_bank_, l2_addr);
            uint8_t byte = ram.read(ram_addr);

            // High nibble = left pixel (sc(1)=0 in VHDL).
            uint8_t left_nib = (byte >> 4) & 0x0F;
            // Apply palette offset: pixel_pre = 0000_XXXX, then offset added to top nibble.
            uint8_t colour_idx = static_cast<uint8_t>((palette_offset_ << 4) | left_nib);

            if (palette.layer2_rgb8(colour_idx) == transp_rgb)
                continue;

            dst[pixel_x] = palette.layer2_colour(colour_idx);
        }
    }
}

void Layer2::save_state(StateWriter& w) const
{
    w.write_u8(active_bank_);
    w.write_u8(shadow_bank_);
    w.write_u16(scroll_x_);
    w.write_u8(scroll_y_);
    w.write_u8(palette_offset_);
    w.write_u8(resolution_);
    w.write_bool(enabled_);
    w.write_u8(clip_x1_); w.write_u8(clip_x2_);
    w.write_u8(clip_y1_); w.write_u8(clip_y2_);
}

void Layer2::load_state(StateReader& r)
{
    active_bank_ = r.read_u8();
    shadow_bank_ = r.read_u8();
    scroll_x_ = r.read_u16();
    scroll_y_ = r.read_u8();
    palette_offset_ = r.read_u8();
    resolution_ = r.read_u8();
    enabled_ = r.read_bool();
    clip_x1_ = r.read_u8(); clip_x2_ = r.read_u8();
    clip_y1_ = r.read_u8(); clip_y2_ = r.read_u8();
}
