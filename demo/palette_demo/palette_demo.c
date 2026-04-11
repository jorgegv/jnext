/*
 * ZX Spectrum Next -- ULA palette demo with colour cycling
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 -subtype=nex \
 *       palette_demo.c -o palette_demo -create-app
 *
 * Produces palette_demo.nex loadable in CSpect / ZEsarUX / real hardware.
 *
 * Demonstrates:
 *   - Drawing colour bars on the ULA screen using all 16 attribute
 *     combinations (8 colours x normal/bright)
 *   - Programming custom ULA palette colours via NextREG RGB333
 *   - Animating palette entries each frame for a colour-cycling effect
 *   - IM1 + HALT frame sync
 *
 * NextREG palette registers used:
 *   0x40 - Palette index (auto-increments after write to 0x41)
 *   0x41 - Palette value (8-bit RRRGGGBB)
 *   0x43 - Palette control (bits [6:4] select palette, bit 0 active pal)
 *   0x14 - Global transparency colour
 *   0x4A - Fallback colour
 *   0x42 - ULA Next attribute byte format
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>   /* ZXN_WRITE_REG() macro                      */
#include <intrinsic.h>  /* intrinsic_halt(), intrinsic_ei()            */

/* ------------------------------------------------------------------ *
 * NextREG indices                                                      *
 * ------------------------------------------------------------------ */

#define NR_PALETTE_IDX   0x40   /* palette entry index                 */
#define NR_PALETTE_VAL   0x41   /* 8-bit RRRGGGBB value                */
#define NR_PALETTE_CTRL  0x43   /* bits[6:4]=palette select            */
#define NR_TRANSP_COL    0x14   /* global transparency colour          */
#define NR_FALLBACK_COL  0x4A   /* fallback colour                     */
#define NR_ULA_ATTR_FMT  0x42   /* ULA Next attribute byte format      */
#define NR_ENHANCED_ULA  0x43   /* same as PALETTE_CTRL; bit 0 active  */

/* Palette control values for ULA first palette */
#define PAL_ULA_FIRST    0x00   /* bits[6:4]=000 = ULA first palette   */

/* ------------------------------------------------------------------ *
 * ULA screen addresses                                                 *
 * ------------------------------------------------------------------ */

#define ULA_PIXEL_BASE   0x4000
#define ULA_ATTR_BASE    0x5800

/* ------------------------------------------------------------------ *
 * Default ULA palette: 16 entries (0-7 normal, 8-15 bright)           *
 * Format: RRRGGGBB (8-bit, maps to RGB332-ish)                       *
 *                                                                      *
 * Standard ZX colours in RGB333 (9-bit) mapped to 8-bit RRRGGGBB:    *
 *   Black   = 000 000 00  = 0x00                                     *
 *   Blue    = 000 000 10  = 0x02                                     *
 *   Red     = 110 000 00  = 0xC0                                     *
 *   Magenta = 110 000 10  = 0xC2                                     *
 *   Green   = 000 110 00  = 0x18                                     *
 *   Cyan    = 000 110 10  = 0x1A                                     *
 *   Yellow  = 110 110 00  = 0xD8                                     *
 *   White   = 110 110 10  = 0xDA                                     *
 *   (bright versions use 111 instead of 110)                          *
 * ------------------------------------------------------------------ */

static const unsigned char default_pal[16] = {
    /* normal: black,  blue,   red,    magenta, green,  cyan,   yellow, white */
    0x00, 0x02, 0xC0, 0xC2, 0x18, 0x1A, 0xD8, 0xDA,
    /* bright: black,  blue,   red,    magenta, green,  cyan,   yellow, white */
    0x00, 0x03, 0xE0, 0xE3, 0x1C, 0x1F, 0xFC, 0xFF
};

/* Working copy of the palette that we animate */
static unsigned char anim_pal[16];

/* ------------------------------------------------------------------ *
 * Fill the entire ULA pixel area with solid blocks (all bits set)     *
 * ------------------------------------------------------------------ */
static void fill_pixels(void)
{
    unsigned char *p = (unsigned char *)ULA_PIXEL_BASE;
    unsigned int i;
    for (i = 0; i < 6144; i++)
        p[i] = 0xFF;
}

/* ------------------------------------------------------------------ *
 * Compute the ULA pixel address for character row r, pixel line y     *
 * within that row. ZX Spectrum screen layout is non-linear:           *
 *   addr = 0x4000 + ((r & 0x18) << 8) + ((y & 7) << 8)              *
 *          + ((r & 7) << 5) + col                                    *
 * For our purposes we just set attributes (paper colour).             *
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ *
 * Set attributes for colour bars:                                      *
 *  - 8 groups of 3 rows each for normal colours (rows 0-23)          *
 *  - First 8 groups: normal (no bright), paper=colour index           *
 *  - Last 8 groups: bright, paper=colour index                        *
 *                                                                      *
 * Since we have 24 rows and 16 colours, we use 1.5 rows per colour:  *
 *  - Actually: 8 normal + 8 bright = 16 entries, 24 rows available   *
 *  - We'll use rows 0-7 for normal 0-7, rows 8-15 for bright 0-7    *
 *  - Rows 16-23: repeat the pattern for visual fullness              *
 *                                                                      *
 * Attribute byte: [7]=flash [6]=bright [5:3]=paper [2:0]=ink         *
 * We set ink=paper so the entire cell is solid colour.                *
 * ------------------------------------------------------------------ */
static void set_colour_bar_attrs(void)
{
    unsigned char *attr = (unsigned char *)ULA_ATTR_BASE;
    unsigned char row, col;
    unsigned char colour, bright, paper, ink, a;

    for (row = 0; row < 24; row++) {
        if (row < 12) {
            /* Normal colours: rows 0-11, 1.5 rows per colour */
            colour = row / 3;           /* 0-7 across 24 rows */
            if (colour > 7) colour = 7;
            bright = 0;
            /* Use different colours per row for more variety */
            colour = row < 8 ? row : row - 8;
            bright = row >= 8 ? 0x40 : 0;
        } else {
            /* Bright colours: rows 12-23 */
            colour = (row - 12) < 8 ? (row - 12) : 7;
            bright = 0x40;
        }

        /* ink = 7 (white/bright white) so text would be visible,
         * but since pixels are all 0xFF, ink is what shows.
         * Actually with all pixels set to 0xFF, INK colour shows.
         * Set ink = paper so bar is solid; use paper colour as the
         * colour index for the bar. */
        paper = colour;
        ink = colour;
        a = bright | (paper << 3) | ink;

        for (col = 0; col < 32; col++) {
            attr[row * 32 + col] = a;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Better layout: show clear colour bars with labels                   *
 *  Row 0:  black (normal)       Row 12: black (bright)               *
 *  Row 1:  blue                 Row 13: bright blue                  *
 *  Row 2:  red                  Row 14: bright red                   *
 *  ...                          ...                                   *
 *  Row 7:  white                Row 19: bright white                 *
 *  Rows 8-11: border/padding    Rows 20-23: border/padding          *
 *                                                                      *
 * With only 24 rows and 16 colours, let's use:                        *
 *  Rows 0-7:   normal colours 0-7 (paper=ink=colour, no bright)      *
 *  Rows 8-11:  repeat normal 0-3 (padding)                           *
 *  Rows 12-19: bright colours 0-7 (paper=ink=colour, bright=1)       *
 *  Rows 20-23: repeat bright 0-3 (padding)                           *
 *                                                                      *
 * Actually, simpler: just use all 24 rows with wrapping.              *
 * ------------------------------------------------------------------ */
static void set_attrs_colour_bars(void)
{
    unsigned char *attr = (unsigned char *)ULA_ATTR_BASE;
    unsigned char row, col;
    unsigned char colour, bright, a;

    for (row = 0; row < 24; row++) {
        if (row < 8) {
            /* Normal colours 0-7 */
            colour = row;
            bright = 0x00;
        } else if (row < 12) {
            /* Repeat first 4 normal for visual fill */
            colour = row - 8;
            bright = 0x00;
        } else if (row < 20) {
            /* Bright colours 0-7 */
            colour = row - 12;
            bright = 0x40;
        } else {
            /* Repeat first 4 bright for visual fill */
            colour = row - 20;
            bright = 0x40;
        }

        /* ink = paper = colour so the bar is solid */
        a = bright | (colour << 3) | colour;

        for (col = 0; col < 32; col++) {
            attr[row * 32 + col] = a;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Upload the 16-entry ULA palette to NextREG                          *
 * ------------------------------------------------------------------ */
static void upload_ula_palette(const unsigned char *pal)
{
    unsigned char i;

    /* Select ULA first palette for writing */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, PAL_ULA_FIRST);

    /* Set starting index to 0 */
    ZXN_WRITE_REG(NR_PALETTE_IDX, 0);

    /* Write all 16 entries (auto-increment) */
    for (i = 0; i < 16; i++) {
        ZXN_WRITE_REG(NR_PALETTE_VAL, pal[i]);
    }
}

/* ------------------------------------------------------------------ *
 * Colour cycling: rotate hue of each palette entry                    *
 *                                                                      *
 * We extract R, G, B components from RRRGGGBB, rotate them through    *
 * a simple hue shift, and repack.                                     *
 *                                                                      *
 * Strategy: each frame, add a phase offset to each colour's hue.      *
 * We precompute a rainbow of 48 colours and index into it.            *
 * ------------------------------------------------------------------ */

/* 48-step rainbow in RRRGGGBB format */
static const unsigned char rainbow[48] = {
    /* Red to Yellow (R=7, G increases) */
    0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC,
    /* Yellow to Green (G=7, R decreases) */
    0xFC, 0xDC, 0xBC, 0x9C, 0x7C, 0x5C, 0x3C, 0x1C,
    /* Green to Cyan (G=7, B increases) */
    0x1C, 0x1D, 0x1E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
    /* Cyan to Blue (B=3, G decreases) */
    0x1F, 0x17, 0x0F, 0x07, 0x07, 0x07, 0x03, 0x03,
    /* Blue to Magenta (B=3, R increases) */
    0x03, 0x23, 0x43, 0x63, 0x83, 0xA3, 0xC3, 0xE3,
    /* Magenta to Red (R=7, B decreases) */
    0xE3, 0xE2, 0xE1, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0,
};

/* ------------------------------------------------------------------ *
 * main                                                                  *
 * ------------------------------------------------------------------ */
int main(void)
{
    unsigned char frame_counter = 0;
    unsigned char i, idx;

    /* 1. Set ULA enhanced palette mode: use ULA first palette,
     *    enable auto-increment (bit 7 = 0), select first palette
     *    as active (bit 0 = 0). */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, PAL_ULA_FIRST);

    /* 2. Set global transparency colour (default 0xE3) */
    ZXN_WRITE_REG(NR_TRANSP_COL, 0xE3);

    /* 3. Set fallback colour to black */
    ZXN_WRITE_REG(NR_FALLBACK_COL, 0x00);

    /* 4. Initialize the working palette from defaults */
    for (i = 0; i < 16; i++) {
        anim_pal[i] = default_pal[i];
    }

    /* 5. Upload initial palette */
    upload_ula_palette(anim_pal);

    /* 6. Fill ULA pixel area with solid blocks */
    fill_pixels();

    /* 7. Set up colour bar attributes */
    set_attrs_colour_bars();

    /* 8. Set border to black (colour index 0, which stays black) */
    __asm__("ld a,0\nout (0xfe),a");

    /* 9. Ensure IM 1 + EI so HALT can return on the frame interrupt */
    intrinsic_im_1();
    intrinsic_ei();

    /* 10. Main loop: animate palette each frame */
    while (1) {
        intrinsic_halt();

        frame_counter++;

        /* Update palette entries 1-7 (normal) and 9-15 (bright)
         * with cycling rainbow colours.
         * Entry 0 (black normal) and 8 (black bright) stay black. */
        for (i = 1; i < 8; i++) {
            /* Each colour gets a different phase offset */
            idx = (unsigned char)((frame_counter + i * 6) % 48);
            anim_pal[i] = rainbow[idx];

            /* Bright version: same hue but boosted
             * (set highest bits in each channel) */
            idx = (unsigned char)((frame_counter + i * 6 + 3) % 48);
            anim_pal[i + 8] = rainbow[idx];
        }

        /* Keep black entries black */
        anim_pal[0] = 0x00;
        anim_pal[8] = 0x00;

        /* Upload the modified palette */
        upload_ula_palette(anim_pal);
    }

    return 0;
}
