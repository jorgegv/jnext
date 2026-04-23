#include "video/ula.h"
#include "video/palette.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "core/log.h"
#include "core/saveable.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// ULA colour lookup — uses PaletteManager if wired, else hardcoded palette
// ---------------------------------------------------------------------------

inline uint32_t Ula::lookup_colour(uint8_t idx) const
{
    if (palette_)
        return palette_->ula_colour(idx);
    return kUlaPalette[idx & 0x0F];
}

// ---------------------------------------------------------------------------
// vram_read — direct physical bank 5/7 access, bypassing MMU
// ---------------------------------------------------------------------------
//
// On real hardware (VHDL), the ULA reads from a dedicated dual-port RAM for
// bank 5 (and bank 7 for shadow/alternate screen). The ULA address bus is
// 14 bits (0x0000-0x3FFF within the bank). The CPU address 0x4000-0x7FFF
// maps to bank 5 physically, but the ULA ignores the MMU — it always reads
// from the physical bank.
//
// Bank 5 = 16K pages 10 and 11 in physical RAM.
// Bank 7 = 16K pages 14 and 15 (for alternate screen / hi-colour attr).

uint8_t Ula::vram_read(uint16_t addr, Mmu& mmu) const
{
    if (ram_) {
        // Direct physical access, bypassing the MMU — as on real hardware.
        // addr is in CPU space; we only care about the 14-bit bank offset.
        // Normal screen  : bank 5, starting at physical page 10.
        // Shadow screen  : bank 7, starting at physical page 14.
        //   (VHDL: ula_bank_do <= vram_bank7_do when port_7ffd_shadow='1')
        //   Bank 7 is implemented as 8K BRAM on the FPGA; the screen data
        //   (pixels + attrs, ~7KB) fits within the first 8K (page 14).
        const uint16_t offset = addr & 0x3FFF;
        const uint32_t page   = vram_use_bank7_ ? 14u : 10u;
        return ram_->read(page * 8192u + offset);
    }
    // Fallback: read through MMU (used when RAM not wired, e.g. early tests).
    return mmu.read(addr);
}

// ---------------------------------------------------------------------------
// set_screen_mode
// ---------------------------------------------------------------------------

void Ula::set_screen_mode(uint8_t port_val)
{
    screen_mode_reg_ = port_val;

    // Mode is encoded in bits 5:3 of port 0xFF (jnext convention).  Per VHDL
    // zxula.vhd:191, `screen_mode_s <= i_port_ff_reg(2 downto 0)`; VHDL bit 0 of
    // that 3-bit mode field is the alt-file select (0 = primary 0x4000 pixel
    // base, 1 = alternate 0x6000 pixel base) — see zxula.vhd:218 and the
    // `vram_a <= screen_mode(0) & …` fetches at zxula.vhd:235/245.  In jnext's
    // port-bit convention that corresponds to bit 0 of `mode_bits`.
    const uint8_t mode_bits = (port_val >> 3) & 0x07;
    // Wave D (S5.04) — alt-file bit tracks mode_bits(0).  Keep `alt_file_`
    // consistent with the last write to port 0xFF so that HI_COLOUR (mode 010)
    // vs. HI_COLOUR+alt (mode 011) can be distinguished by the renderer.
    alt_file_ = (mode_bits & 0x01) != 0;
    switch (mode_bits) {
        case 0:                                       // 000 = STANDARD
        case 1: mode_ = (mode_bits == 0)              // 001 = STANDARD+alt
                        ? TimexScreenMode::STANDARD
                        : TimexScreenMode::STANDARD_1;
                break;
        case 2:                                       // 010 = HI_COLOUR
        case 3: mode_ = TimexScreenMode::HI_COLOUR;   // 011 = HI_COLOUR+alt
                break;                                // (alt_file_ disambiguates)
        case 6:                                       // 110 = HI_RES
        case 7: mode_ = TimexScreenMode::HI_RES;      // 111 = HI_RES+alt
                break;                                // (alt_file_ disambiguates)
        default:
            Log::ula()->warn("Unknown Timex screen mode bits {:#04x}, defaulting to STANDARD", mode_bits);
            mode_ = TimexScreenMode::STANDARD;
            break;
    }
    Log::ula()->debug("Screen mode set: port_val={:#04x} mode={} alt_file={}",
                      port_val, static_cast<int>(mode_), alt_file_);
}

// ---------------------------------------------------------------------------
// pixel_addr_offset
// ---------------------------------------------------------------------------
//
// ZX Spectrum screen layout in VRAM (starting at 0x4000):
//
//   Pixel bytes:     0x4000 – 0x57FF  (6 KiB, 192 rows × 32 bytes/row)
//   Attribute bytes: 0x5800 – 0x5AFF  (768 bytes, 24 attr-rows × 32 bytes)
//
// The pixel address is NOT simply row * 32 + col.  The screen is divided into
// three 64-row thirds, and within each third the rows are stored
// interleaved by groups of 8:
//
//   pixel_addr = base
//              | ((screen_row & 0xC0) << 5)   // third select  → bits 12:11
//              | ((screen_row & 0x07) << 8)   // fine row       → bits 10:8
//              | ((screen_row & 0x38) << 2)   // coarse row     → bits 7:5
//              | col                           // column (0–31)  → bits 4:0

uint16_t Ula::pixel_addr_offset(int screen_row, int col)
{
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// ---------------------------------------------------------------------------
// Scroll folds (VHDL zxula.vhd:192-207 for Y, :199 for X)
// ---------------------------------------------------------------------------
//
// Y-fold (zxula.vhd:192-207):
//   py_s <= i_vc + ('0' & i_ula_scroll_y);            -- 9-bit add
//   if py_s(8 downto 7) = "11" then                   -- py_s ∈ [384, 511]
//      py <= (not py_s(7)) & py_s(6 downto 0);        -- clear bit 7 (−128)
//   elsif py_s(8) = '1' or py_s(7 downto 6) = "11" then   -- py_s ∈ [192,383]
//      py <= (py_s(7 downto 6) + 1) & py_s(5 downto 0);   -- +64 in bits(7:6),
//                                                          -- mod 4 with wrap
//   else
//      py <= py_s(7 downto 0);                        -- passthrough
//   end if;
// Functionally this is (vc + scroll_y) mod 192 with the specific encoding the
// hardware uses to preserve third-cycling in the subsequent address
// calculation `addr_p_spc_12_5 <= py(7:6) & py(2:0) & py(5:3)` (zxula.vhd:223).
static int fold_ula_y(int raw_y, uint8_t scroll_y)
{
    const int py_s = (raw_y & 0x1FF) + scroll_y;  // 9-bit result
    const int b87  = (py_s >> 7) & 0x3;
    const int b8   = (py_s >> 8) & 0x1;
    const int b76  = (py_s >> 6) & 0x3;
    if (b87 == 0x3) {
        // py_s ∈ [384, 511] : clear bit 7.
        return py_s & 0x7F;
    } else if (b8 == 1 || b76 == 0x3) {
        // py_s ∈ [192, 383] : add 64 to bits(7:6), low 6 bits pass through.
        const int new76 = (((py_s >> 6) & 0x3) + 1) & 0x3;
        return static_cast<int>((new76 << 6) | (py_s & 0x3F));
    }
    return py_s & 0xFF;
}

// X-fold (zxula.vhd:199):
//   px <= i_ula_fine_scroll_x & (i_hc(7:3) + i_ula_scroll_x(7:3)) &
//         i_ula_scroll_x(2:0);
// with px_1 <= px(8) & (px(7:3)+1) & px(2:0) (line 216) feeding the vram_a
// byte-column field and px(2:0) acting as the bit-within-byte phase. The
// observable effect at output-pixel granularity is that output pixel at
// display column X reads source pixel at
//   src_x = (X + scroll_x + fine_scroll_x) mod 256.
// NR 0x26 is an 8-bit pixel offset: bits(7:3) are byte-column shift
// (scroll_x(7:3) * 8 pixels), bits(2:0) are bit-within-byte shift. Since the
// byte-shift already multiplies by 8, the raw 8-bit value IS the pixel shift.
static int fold_ula_x(int raw_x, uint8_t scroll_x, bool fine_scroll_x)
{
    return (raw_x + static_cast<int>(scroll_x) + (fine_scroll_x ? 1 : 0)) & 0xFF;
}

// ---------------------------------------------------------------------------
// render_frame
// ---------------------------------------------------------------------------

void Ula::render_frame(uint32_t* framebuffer, Mmu& mmu)
{
    // The 320×256 output framebuffer is laid out as follows:
    //
    //   Rows   0..DISP_Y-1              → top border  (DISP_Y = 32)
    //   Rows   DISP_Y..DISP_Y+DISP_H-1 → display area (192 rows)
    //   Rows   DISP_Y+DISP_H..FB_HEIGHT-1 → bottom border
    //
    // Framebuffer height is 256, giving symmetric borders:
    //   top = 32, display = 192, bottom = 256 - 32 - 192 = 32.

    for (int row = 0; row < FB_HEIGHT; ++row) {
        uint32_t* line = framebuffer + row * FB_WIDTH;
        const int screen_row = row - DISP_Y;   // < 0 if in top border

        // Use per-line border colour (snapshotted during frame execution).
        border_colour_ = border_per_line_[row];

        if (screen_row >= 0 && screen_row < DISP_H) {
            switch (mode_) {
                case TimexScreenMode::STANDARD: {
                    // Primary screen: pixels at 0x4000, attrs at 0x5800.
                    const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                    const uint16_t attr_row = static_cast<uint16_t>(
                        0x5800u + (screen_row / 8) * 32);
                    render_display_line(line, screen_row, poff, attr_row, mmu);
                    break;
                }
                case TimexScreenMode::STANDARD_1: {
                    // Alternate screen: pixels at 0x6000, attrs at 0x7800.
                    const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                    const uint16_t attr_row = static_cast<uint16_t>(
                        0x7800u + (screen_row / 8) * 32);
                    // pixel_base_offset is used as an offset from 0x6000 in
                    // render_display_line — see that function's implementation.
                    render_display_line(line, screen_row, poff, attr_row, mmu);
                    break;
                }
                case TimexScreenMode::HI_COLOUR:
                    render_display_line_hicolour(line, screen_row, mmu);
                    break;
                case TimexScreenMode::HI_RES:
                    render_display_line_hires(line, screen_row, mmu);
                    break;
            }
        } else {
            // Top or bottom border: fill entire row with border colour.
            render_border_line(line);
        }
    }

    // Advance flash state once per frame.
    advance_flash();
}

// ---------------------------------------------------------------------------
// render_scanline — render a single row for the compositor
// ---------------------------------------------------------------------------

void Ula::render_scanline(uint32_t* dst, int row, Mmu& mmu)
{
    // Temporarily use the per-line border colour for rendering, but restore
    // the live border_colour_ afterwards so init_border_per_line() at the
    // next frame start uses the value last set by port 0xFE, not the
    // per-line snapshot from the last rendered row.
    const uint8_t saved_border = border_colour_;
    if (row >= 0 && row < FB_HEIGHT)
        border_colour_ = border_per_line_[row];

    const int screen_row = row - DISP_Y;

    if (screen_row >= 0 && screen_row < DISP_H) {
        switch (mode_) {
            case TimexScreenMode::STANDARD: {
                const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                const uint16_t attr_row = static_cast<uint16_t>(
                    0x5800u + (screen_row / 8) * 32);
                render_display_line(dst, screen_row, poff, attr_row, mmu);
                break;
            }
            case TimexScreenMode::STANDARD_1: {
                const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                const uint16_t attr_row = static_cast<uint16_t>(
                    0x7800u + (screen_row / 8) * 32);
                render_display_line(dst, screen_row, poff, attr_row, mmu);
                break;
            }
            case TimexScreenMode::HI_COLOUR:
                render_display_line_hicolour(dst, screen_row, mmu);
                break;
            case TimexScreenMode::HI_RES:
                render_display_line_hires(dst, screen_row, mmu);
                break;
        }
    } else {
        render_border_line(dst);
    }

    border_colour_ = saved_border;
}

// ---------------------------------------------------------------------------
// render_scanline_screen1 — render 128K shadow screen (bank 7) for debug panel
// ---------------------------------------------------------------------------
//
// Bank 7 uses the identical ZX pixel/attribute layout as bank 5, just stored
// in physical pages 14-15.  We temporarily set vram_use_bank7_=true so that
// all vram_read() calls inside render_display_line() go to page 14 instead of
// page 10.  We pass STANDARD-mode addresses (0x4000-base) so that
// render_display_line() uses pixel_base = 0x4000 | poff (the 0x3FFF mask in
// vram_read strips that base, leaving just the 14-bit bank offset).

void Ula::render_scanline_screen1(uint32_t* dst, int row, Mmu& mmu)
{
    const uint8_t saved_border   = border_colour_;
    const bool    saved_bank7    = vram_use_bank7_;

    if (row >= 0 && row < FB_HEIGHT)
        border_colour_ = border_per_line_[row];

    vram_use_bank7_ = true;

    const int screen_row = row - DISP_Y;

    if (screen_row >= 0 && screen_row < DISP_H) {
        // STANDARD-mode addresses: pixel base 0x4000, attr base 0x5800.
        // vram_read will map the 14-bit offsets to bank 7 (page 14).
        const uint16_t poff     = pixel_addr_offset(screen_row, 0);
        const uint16_t attr_row = static_cast<uint16_t>(0x5800u + (screen_row / 8) * 32);
        render_display_line(dst, screen_row, poff, attr_row, mmu);
    } else {
        render_border_line(dst);
    }

    vram_use_bank7_ = saved_bank7;
    border_colour_  = saved_border;
}

// ---------------------------------------------------------------------------
// advance_flash — call once per frame after rendering all scanlines
// ---------------------------------------------------------------------------

void Ula::advance_flash()
{
    ++flash_counter_;
    if (flash_counter_ >= 16) {
        flash_counter_ = 0;
        flash_phase_   = !flash_phase_;
    }
}

// ---------------------------------------------------------------------------
// render_display_line  (STANDARD and STANDARD_1)
// ---------------------------------------------------------------------------
//
// Parameters:
//   pixel_base_offset  — interleaved row offset (from pixel_addr_offset for
//                        column 0); added to 0x4000 (STANDARD) or 0x6000
//                        (STANDARD_1).
//   attr_row_base      — base address of the 32-byte attribute row for this
//                        display line (e.g. 0x5800 + (row/8)*32 for primary,
//                        0x7800 + (row/8)*32 for alternate).
//
// The pixel_base_offset already encodes the interleaved row; column bytes are
// read at pixel_base_offset + col.  For STANDARD_1 the base is shifted to
// 0x6000 — this function receives the base as (0x4000|poff) or (0x6000|poff)
// via the caller, so we keep the same logic.
//
// NOTE: the caller in render_frame passes pixel_addr_offset(screen_row, 0)
// (column-zero offset) and the appropriate attr_row_base.  For STANDARD_1 the
// pixel_base is rebuilt here by OR-ing with 0x6000 instead of 0x4000.

void Ula::render_display_line(uint32_t* row, int screen_row,
                               uint16_t pixel_base_offset,
                               uint16_t attr_row_base,
                               Mmu& mmu)
{
    // Apply the ULA Y-scroll fold per zxula.vhd:192-207. With scroll_y==0
    // this returns screen_row unchanged so the default no-scroll case is
    // unaffected. Cross-third wrap (scroll_y=64 etc.) is handled by the
    // fold because py(7:6) is re-encoded before addr_p_spc_12_5 is formed
    // (zxula.vhd:223).
    const int eff_row = fold_ula_y(screen_row, ula_scroll_y_);

    // The caller hands us pixel_base_offset = pixel_addr_offset(screen_row, 0);
    // regenerate it from the folded row so the pixel/attr addresses reflect
    // the scrolled line. If scroll_y is zero this matches the incoming value.
    const bool alt = (attr_row_base >= 0x7800);
    (void)pixel_base_offset;  // regenerated below for correctness under scroll
    const uint16_t eff_poff      = pixel_addr_offset(eff_row, 0);
    const uint16_t pixel_base    = static_cast<uint16_t>((alt ? 0x6000u : 0x4000u) | eff_poff);
    const uint16_t eff_attr_base = static_cast<uint16_t>((alt ? 0x7800u : 0x5800u)
                                                         + (eff_row / 8) * 32);

    // Fill left border pixels (DISP_X = 32 pixels).
    const uint32_t border_argb = lookup_colour(border_colour_);
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    // X-scroll fold per zxula.vhd:199: source pixel for display pixel X is
    //   src_x = (X + scroll_x + fine_scroll_x) mod 256.
    // When there is no X scroll we keep the fast column-oriented loop so the
    // no-scroll default path is byte-for-byte identical to the pre-scroll
    // implementation.
    const uint8_t scroll_x = ula_scroll_x_coarse_;
    const bool    fine     = ula_fine_scroll_x_;

    if (scroll_x == 0 && !fine) {
        // Fast path (unchanged semantics under default NR 0x26/NR 0x68 bit 2).
        for (int col = 0; col < 32; ++col) {
            const uint8_t pixels = vram_read(static_cast<uint16_t>(pixel_base + col), mmu);
            const uint8_t attr   = vram_read(static_cast<uint16_t>(eff_attr_base + col), mmu);

            const bool flash_a = (attr & 0x80) != 0;
            const bool bright  = (attr & 0x40) != 0;
            uint8_t paper = (attr >> 3) & 0x07;
            uint8_t ink   =  attr       & 0x07;

            if (flash_a && flash_phase_) {
                uint8_t tmp = ink; ink = paper; paper = tmp;
            }

            const uint8_t ink_idx   = ink   + (bright ? 8 : 0);
            const uint8_t paper_idx = paper + (bright ? 8 : 0);

            const uint32_t ink_argb   = lookup_colour(ink_idx);
            const uint32_t paper_argb = lookup_colour(paper_idx);

            uint32_t* dst = row + DISP_X + col * 8;
            for (int bit = 7; bit >= 0; --bit) {
                *dst++ = (pixels >> bit) & 1 ? ink_argb : paper_argb;
            }
        }
    } else {
        // Scrolled path: per-pixel source lookup. The VHDL shift-register
        // emits px(7:3) (byte column) and px(2:0) (within-byte bit phase)
        // with px(8) adding a 1-pixel fine offset (line 199 + 216). At the
        // observable output granularity that collapses to fold_ula_x().
        for (int disp_x = 0; disp_x < 256; ++disp_x) {
            const int src_x  = fold_ula_x(disp_x, scroll_x, fine);
            const int src_col = src_x >> 3;          // byte column 0..31
            const int src_bit = 7 - (src_x & 0x7);   // MSB-first within byte
            const uint8_t pixels = vram_read(
                static_cast<uint16_t>(pixel_base + src_col), mmu);
            const uint8_t attr   = vram_read(
                static_cast<uint16_t>(eff_attr_base + src_col), mmu);

            const bool flash_a = (attr & 0x80) != 0;
            const bool bright  = (attr & 0x40) != 0;
            uint8_t paper = (attr >> 3) & 0x07;
            uint8_t ink   =  attr       & 0x07;
            if (flash_a && flash_phase_) {
                uint8_t tmp = ink; ink = paper; paper = tmp;
            }
            const uint8_t ink_idx   = ink   + (bright ? 8 : 0);
            const uint8_t paper_idx = paper + (bright ? 8 : 0);
            const uint32_t ink_argb   = lookup_colour(ink_idx);
            const uint32_t paper_argb = lookup_colour(paper_idx);

            const bool pix_on = ((pixels >> src_bit) & 1) != 0;
            row[DISP_X + disp_x] = pix_on ? ink_argb : paper_argb;
        }
    }

    // Fill right border pixels (FB_WIDTH - DISP_X - DISP_W = 32 pixels).
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x)
        right[x] = border_argb;
}

// ---------------------------------------------------------------------------
// render_display_line_hicolour  (HI_COLOUR mode)
// ---------------------------------------------------------------------------
//
// Timex hi-colour mode: 256×192 with per-cell (8×1) colour attributes.
//
// Pixel data:    read from primary screen 0x4000 using the standard ZX
//               interleaved addressing.
// Attribute data: each attribute byte controls one 8-pixel column within a
//               single display row (not an 8-row character cell as in standard
//               mode).  The layout of the attribute area at 0x6000 mirrors the
//               standard pixel layout:
//
//                 attr_addr = 0x6000 | pixel_addr_offset(screen_row, col)
//
//               This matches the Timex hi-colour specification: the 6 KiB
//               attribute area starting at 0x6000 uses the same interleaved
//               row addressing as the pixel area at 0x4000, giving one
//               independent attribute byte per 8-pixel column per scanline.
//
// Flash and bright are derived from the attribute byte exactly as in standard
// mode.

void Ula::render_display_line_hicolour(uint32_t* row, int screen_row, Mmu& mmu)
{
    const uint16_t poff = pixel_addr_offset(screen_row, 0);
    // Wave D (S5.04) — VHDL zxula.vhd:218/235 shows the pixel vram_a uses
    // `screen_mode(0) & addr_p_spc_12_5 & px_1(7:3)`: in hi-colour mode the
    // pixel base follows the alt-file bit (0x4000 when alt=0, 0x6000 when
    // alt=1).  The attribute base stays at bank 1 (0x6000) because the
    // attribute fetch uses `'1' & addr_p_spc_12_5 & px_1(7:3)` when
    // screen_mode(1)='1' (zxula.vhd:239/249).  In HI_COLOUR+alt the pixel
    // and attribute bytes collide at the same address — a VHDL-literal
    // consequence that Wave D preserves.
    const uint16_t pixel_base = static_cast<uint16_t>(
        (alt_file_ ? 0x6000u : 0x4000u) | poff);
    const uint16_t attr_base  = static_cast<uint16_t>(0x6000u | poff);

    // Fill left border.
    const uint32_t border_argb = lookup_colour(border_colour_);
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    for (int col = 0; col < 32; ++col) {
        const uint8_t pixels = vram_read(static_cast<uint16_t>(pixel_base + col), mmu);
        const uint8_t attr   = vram_read(static_cast<uint16_t>(attr_base  + col), mmu);

        const bool flash  = (attr & 0x80) != 0;
        const bool bright = (attr & 0x40) != 0;
        uint8_t paper = (attr >> 3) & 0x07;
        uint8_t ink   =  attr       & 0x07;

        if (flash && flash_phase_) {
            uint8_t tmp = ink; ink = paper; paper = tmp;
        }

        const uint8_t ink_idx   = ink   + (bright ? 8 : 0);
        const uint8_t paper_idx = paper + (bright ? 8 : 0);

        const uint32_t ink_argb   = lookup_colour(ink_idx);
        const uint32_t paper_argb = lookup_colour(paper_idx);

        uint32_t* dst = row + DISP_X + col * 8;
        for (int bit = 7; bit >= 0; --bit) {
            *dst++ = (pixels >> bit) & 1 ? ink_argb : paper_argb;
        }
    }

    // Fill right border.
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x)
        right[x] = border_argb;
}

// ---------------------------------------------------------------------------
// render_display_line_hires  (HI_RES mode)
// ---------------------------------------------------------------------------
//
// Timex hi-res mode provides 512 monochrome pixels per scanline by
// interleaving two 256-pixel-wide screens:
//   - Screen 0 (0x4000): provides odd display columns (columns 1, 3, 5, …)
//   - Screen 1 (0x6000): provides even display columns (columns 0, 2, 4, …)
//
// LIMITATION: the output framebuffer is only 320 pixels wide (256 active
// display pixels).  A true 512-pixel render does not fit.  This implementation
// renders at 256 output pixels by discarding every second hi-res pixel
// (one output pixel per two hi-res pixels, taken from the most-significant
// bit of each pair).  This halves the horizontal resolution but keeps aspect
// ratio consistent with the other modes.
//
// Ink colour  = bits 2:0 of the last value written to port 0xFF (screen_mode_reg_).
// Paper colour = bits 5:3 of screen_mode_reg_.
// Bright flag is not available in hi-res mode; colours are non-bright (indices 0–7).

void Ula::render_display_line_hires(uint32_t* row, int screen_row, Mmu& mmu)
{
    // Decode ink / paper from port 0xFF register value.
    const uint8_t ink_idx   = screen_mode_reg_ & 0x07;
    const uint8_t paper_idx = (screen_mode_reg_ >> 3) & 0x07;

    const uint32_t ink_argb   = lookup_colour(ink_idx);
    const uint32_t paper_argb = lookup_colour(paper_idx);
    const uint32_t border_argb = lookup_colour(border_colour_);

    // The interleaved pixel row offset is the same for both screens.
    const uint16_t poff = pixel_addr_offset(screen_row, 0);
    const uint16_t screen0_base = static_cast<uint16_t>(0x4000u | poff);
    const uint16_t screen1_base = static_cast<uint16_t>(0x6000u | poff);

    // Fill left border.
    for (int x = 0; x < DISP_X; ++x)
        row[x] = border_argb;

    // Each of the 32 display columns produces 16 hi-res pixels (8 from screen 0,
    // 8 from screen 1, interleaved).  We render them as 8 output pixels (one per
    // hi-res pair) to fit within the 256-pixel display width.
    //
    // Column interleaving: for display column c (0–31):
    //   screen1 byte  → bits 7,5,3,1 of hi-res pixels (even hi-res cols)
    //   screen0 byte  → bits 6,4,2,0 of hi-res pixels (odd hi-res cols)
    //
    // The hi-res columns run left-to-right as: s1_bit7, s0_bit7, s1_bit6,
    // s0_bit6, …  i.e. screen 1 contributes the first pixel of each pair.
    // We output one pixel per pair (the screen-1 pixel = left pixel of pair).

    for (int col = 0; col < 32; ++col) {
        const uint8_t b0 = vram_read(static_cast<uint16_t>(screen0_base + col), mmu);
        const uint8_t b1 = vram_read(static_cast<uint16_t>(screen1_base + col), mmu);

        uint32_t* dst = row + DISP_X + col * 8;
        for (int bit = 7; bit >= 0; --bit) {
            // Take the screen-1 bit as the representative pixel for this pair.
            *dst++ = (b1 >> bit) & 1 ? ink_argb : paper_argb;
        }
    }

    // Fill right border.
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x)
        right[x] = border_argb;
}

// ---------------------------------------------------------------------------
// render_border_line
// ---------------------------------------------------------------------------

void Ula::render_border_line(uint32_t* row)
{
    // Default route: standard `border_clr` from port 0xFE bits 2:0
    // (zxula.vhd:418).  Hi-res route: when `border_clr_tmx_src_` is set OR the
    // current Timex screen mode is HI_RES, use `border_clr_tmx` (zxula.vhd:419,
    // :443-448): in VHDL the attr_reg is loaded with `border_clr_tmx` whenever
    // `shift_screen_mode(2)='1'`.  jnext approximates the 6-bit tmx colour by
    // taking port_ff paper bits (5:3) as a 0–7 palette index, since we don't
    // plumb the full 6-bit `"01" & not paper & paper` palette-group encoding
    // — a documented limitation tracked by S5.06's one-line citation.
    const bool     use_tmx = border_clr_tmx_src_
                             || (mode_ == TimexScreenMode::HI_RES);
    const uint8_t  idx     = use_tmx
        ? static_cast<uint8_t>((screen_mode_reg_ >> 3) & 0x07)
        : border_colour_;
    const uint32_t border_argb = lookup_colour(idx);
    for (int x = 0; x < FB_WIDTH; ++x)
        row[x] = border_argb;
}

void Ula::save_state(StateWriter& w) const
{
    w.write_bool(ula_enabled_);
    w.write_bool(vram_use_bank7_);
    w.write_u8(clip_x1_); w.write_u8(clip_x2_);
    w.write_u8(clip_y1_); w.write_u8(clip_y2_);
    w.write_u8(border_colour_);
    w.write_bytes(border_per_line_.data(), FB_HEIGHT);
    w.write_i32(flash_counter_);
    w.write_bool(flash_phase_);
    w.write_u8(screen_mode_reg_);
    w.write_u8(static_cast<uint8_t>(mode_));

    // Phase-1 scaffold state — appended so legacy snapshots still load the
    // prefix cleanly while new snapshots round-trip the full register file.
    w.write_u8(ula_scroll_x_coarse_);
    w.write_u8(ula_scroll_y_);
    w.write_bool(ula_fine_scroll_x_);
    w.write_u8(ulanext_format_);
    w.write_bool(ulanext_en_);
    w.write_bool(ulap_en_);
    w.write_bool(alt_file_);
    w.write_bool(shadow_screen_en_);
    w.write_bool(border_clr_tmx_src_);
}

void Ula::load_state(StateReader& r)
{
    ula_enabled_ = r.read_bool();
    vram_use_bank7_ = r.read_bool();
    clip_x1_ = r.read_u8(); clip_x2_ = r.read_u8();
    clip_y1_ = r.read_u8(); clip_y2_ = r.read_u8();
    border_colour_ = r.read_u8();
    r.read_bytes(border_per_line_.data(), FB_HEIGHT);
    flash_counter_ = r.read_i32();
    flash_phase_ = r.read_bool();
    screen_mode_reg_ = r.read_u8();
    mode_ = static_cast<TimexScreenMode>(r.read_u8());

    ula_scroll_x_coarse_ = r.read_u8();
    ula_scroll_y_        = r.read_u8();
    ula_fine_scroll_x_   = r.read_bool();
    ulanext_format_      = r.read_u8();
    ulanext_en_          = r.read_bool();
    ulap_en_             = r.read_bool();
    alt_file_            = r.read_bool();
    shadow_screen_en_    = r.read_bool();
    border_clr_tmx_src_  = r.read_bool();
}
