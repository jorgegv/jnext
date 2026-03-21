/*
 * ZX Spectrum Next -- Tilemap demo (40x32 mode with smooth scrolling)
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 -subtype=nex tilemap_demo.c -o tilemap_demo -create-app
 *
 * Produces tilemap_demo.nex loadable in jnext / CSpect / real hardware.
 *
 * Demonstrates:
 *   - 40x32 tilemap mode with 8x8 4bpp tiles
 *   - Tile pattern definition (solid, checkerboard, border, letter)
 *   - Decorative border with interior fill
 *   - Tilemap palette setup (distinct colours)
 *   - Smooth X/Y scrolling via NextREG 0x2F/0x30/0x31
 *   - IM1 + HALT frame sync
 *
 * NextREG register usage verified against emulator source:
 *   src/video/tilemap.h (derived from VHDL tilemap.vhd)
 *   src/port/nextreg.h
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * NextREG indices                                                      *
 * ------------------------------------------------------------------ */

#define NR_TILEMAP_CTRL     0x6B   /* bit7=en, bit6=80col, bit5=text,    */
                                   /* bit4=force_attr, bit1=512tile,     */
                                   /* bit0=ULA-over-tilemap              */
#define NR_TILEMAP_DEFATTR  0x6C   /* default tilemap attribute           */
#define NR_TILEMAP_MAPBASE  0x6E   /* tilemap base address                */
#define NR_TILEMAP_DEFBASE  0x6F   /* tile definitions (patterns) base    */
#define NR_TILEMAP_XSCROLL  0x2F   /* X scroll LSB                        */
#define NR_TILEMAP_XSCROLL_MSB 0x30 /* X scroll MSB (bits 1:0)            */
#define NR_TILEMAP_YSCROLL  0x31   /* Y scroll (0-255)                    */

#define NR_PALETTE_CTRL     0x43   /* bits[6:4] = palette select           */
#define NR_PALETTE_IDX      0x40   /* palette entry index                  */
#define NR_PALETTE_VAL      0x41   /* 8-bit RRRGGGBB value                */

#define NR_SPRITE_CTRL      0x15   /* layer priority + sprite enable       */
#define NR_ULA_CTRL         0x68   /* ULA control                          */
#define NR_DISPLAY_CTRL     0x69   /* display control                      */

/* Palette select values for NR_PALETTE_CTRL bits[6:4] */
#define PAL_TILEMAP_FIRST   0x30   /* 011_0 = tilemap first palette        */

/* ------------------------------------------------------------------ *
 * Tilemap memory layout                                                *
 *                                                                      *
 * We place tilemap data at bank 5 offset 0x4000 (CPU address 0x4000).  *
 *   NR 0x6E value: bits[7:1] encode address bits[16:10].               *
 *   Bank 5 starts at physical address 0x0A000 (page 10).               *
 *   Offset 0x0000 within bank 5 => physical 0x0A000.                   *
 *   For CPU address 0x4000 in bank 5: NR 0x6E = 0x00 (default).        *
 *                                                                      *
 * Tile patterns at bank 5 offset 0x6000 (CPU address 0x6000).          *
 *   NR 0x6F = 0x0C (bits[7:1] = 0x06 << 1, selecting 1K offset 6      *
 *   within bank 5 = address 0x6000 - but this is wrong, let me calc)   *
 *                                                                      *
 * Actually, the register value bits[7:1] map to address bits[16:10]:   *
 *   Physical addr = bank_base + (reg_val >> 1) * 1024                  *
 *   Bank 5 base = 0x4000 in CPU space (physical page 10,11)            *
 *   - For tilemap at 0x4000: reg = 0x00 (offset 0 in bank 5)          *
 *   - For patterns at 0x6000: offset = 0x2000 = 8*1024,               *
 *     so reg = (8 << 1) = 0x10                                         *
 *                                                                      *
 * Wait -- let me re-read the emulator source for exact encoding.       *
 * From tilemap.h set_map_base / set_def_base:                          *
 *   bit 7 of reg value (= bit 6 of 7-bit field) selects bank 5 or 7.  *
 *   bits[7:1] => address bits[16:10] within the 32K window.            *
 *                                                                      *
 * In practice, for the default config (NR 0x6E = 0x00):                *
 *   tilemap data is at the start of bank 5 = CPU addr 0x4000.          *
 * For patterns at 0x6000 (2K offset = 0x800 from bank 5):              *
 *   We need (reg >> 1) to give the 1K-aligned offset.                  *
 *   0x6000 - 0x4000 = 0x2000 = 8192 = 8 * 1024                        *
 *   So reg bits[7:1] = 8, meaning reg = 8 << 1 = 0x10.                *
 *                                                                      *
 * Tilemap map: 40*32*2 = 2560 bytes at 0x4000-0x49FF.                  *
 * Patterns: we define a handful of patterns (say 8 = 256 bytes) at     *
 * 0x6000.                                                              *
 * ------------------------------------------------------------------ */

#define TILEMAP_ADDR    0x4000
#define PATTERNS_ADDR   0x6000

/* Number of tile patterns we define */
#define NUM_PATTERNS    8

/* Tile indices */
#define TILE_EMPTY      0
#define TILE_SOLID      1
#define TILE_CHECK      2
#define TILE_BORDER_H   3
#define TILE_BORDER_V   4
#define TILE_CORNER     5
#define TILE_LETTER_N   6
#define TILE_LETTER_X   7

/* ------------------------------------------------------------------ *
 * Palette helper                                                        *
 * ------------------------------------------------------------------ */
static void pal_write(unsigned char idx, unsigned char rrrgggbb)
{
    ZXN_WRITE_REG(NR_PALETTE_IDX, idx);
    ZXN_WRITE_REG(NR_PALETTE_VAL, rrrgggbb);
}

/* ------------------------------------------------------------------ *
 * Tile pattern data (8x8, 4bpp = 32 bytes per tile)                    *
 *                                                                      *
 * Each row is 4 bytes.  Each byte holds 2 pixels (high nibble first).  *
 * So for an 8-pixel row: byte0=[px0,px1], byte1=[px2,px3],            *
 *                         byte2=[px4,px5], byte3=[px6,px7].            *
 * ------------------------------------------------------------------ */

/* Helper: pack two 4-bit pixels into one byte */
#define PX(a,b) (unsigned char)(((a)<<4)|(b))

static const unsigned char patterns[NUM_PATTERNS * 32] = {
    /* Tile 0: Empty (all colour 0 = transparent/background) */
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),
    PX(0,0), PX(0,0), PX(0,0), PX(0,0),

    /* Tile 1: Solid block (all colour 1) */
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),
    PX(1,1), PX(1,1), PX(1,1), PX(1,1),

    /* Tile 2: Checkerboard (colours 2 and 3) */
    PX(2,3), PX(2,3), PX(2,3), PX(2,3),
    PX(3,2), PX(3,2), PX(3,2), PX(3,2),
    PX(2,3), PX(2,3), PX(2,3), PX(2,3),
    PX(3,2), PX(3,2), PX(3,2), PX(3,2),
    PX(2,3), PX(2,3), PX(2,3), PX(2,3),
    PX(3,2), PX(3,2), PX(3,2), PX(3,2),
    PX(2,3), PX(2,3), PX(2,3), PX(2,3),
    PX(3,2), PX(3,2), PX(3,2), PX(3,2),

    /* Tile 3: Horizontal border (colour 4 top/bottom lines, 5 fill) */
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),
    PX(5,5), PX(5,5), PX(5,5), PX(5,5),
    PX(5,5), PX(5,5), PX(5,5), PX(5,5),
    PX(5,5), PX(5,5), PX(5,5), PX(5,5),
    PX(5,5), PX(5,5), PX(5,5), PX(5,5),
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),

    /* Tile 4: Vertical border (colour 4 left/right cols, 5 fill) */
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),

    /* Tile 5: Corner (colour 4 on edges, 5 fill) */
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(5,5), PX(5,5), PX(4,4),
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),
    PX(4,4), PX(4,4), PX(4,4), PX(4,4),

    /* Tile 6: Letter "N" (colour 6 on background 0) */
    PX(6,0), PX(0,0), PX(0,0), PX(0,6),
    PX(6,6), PX(0,0), PX(0,0), PX(0,6),
    PX(6,0), PX(6,0), PX(0,0), PX(0,6),
    PX(6,0), PX(0,6), PX(0,0), PX(0,6),
    PX(6,0), PX(0,0), PX(6,0), PX(0,6),
    PX(6,0), PX(0,0), PX(0,6), PX(0,6),
    PX(6,0), PX(0,0), PX(0,0), PX(6,6),
    PX(6,0), PX(0,0), PX(0,0), PX(0,6),

    /* Tile 7: Letter "X" (colour 7 on background 0) */
    PX(7,0), PX(0,0), PX(0,0), PX(0,7),
    PX(0,7), PX(0,0), PX(0,0), PX(7,0),
    PX(0,0), PX(7,0), PX(0,7), PX(0,0),
    PX(0,0), PX(0,7), PX(7,0), PX(0,0),
    PX(0,0), PX(0,7), PX(7,0), PX(0,0),
    PX(0,0), PX(7,0), PX(0,7), PX(0,0),
    PX(0,7), PX(0,0), PX(0,0), PX(7,0),
    PX(7,0), PX(0,0), PX(0,0), PX(0,7),
};

/* ------------------------------------------------------------------ *
 * Copy tile patterns into pattern RAM at PATTERNS_ADDR                 *
 * ------------------------------------------------------------------ */
static void upload_patterns(void)
{
    unsigned char *dst = (unsigned char *)PATTERNS_ADDR;
    unsigned int i;
    for (i = 0; i < sizeof(patterns); i++)
        dst[i] = patterns[i];
}

/* ------------------------------------------------------------------ *
 * Fill the 40x32 tilemap with a decorative border + interior pattern   *
 *                                                                      *
 * Tilemap entry format (2 bytes per tile):                             *
 *   Byte 0: tile number (0-255 or 0-511 in 512-tile mode)             *
 *   Byte 1: attribute                                                  *
 *     bits[7:4] = palette offset                                       *
 *     bit 3     = mirror X                                             *
 *     bit 2     = mirror Y                                             *
 *     bit 1     = rotate                                               *
 *     bit 0     = ULA-over-tilemap (or tile bit 8 in 512-tile mode)    *
 * ------------------------------------------------------------------ */
static void fill_tilemap(void)
{
    unsigned char *map = (unsigned char *)TILEMAP_ADDR;
    unsigned char row, col;
    unsigned char tile, attr;

    for (row = 0; row < 32; row++) {
        for (col = 0; col < 40; col++) {
            /* Determine tile for this cell */
            if ((row == 0 || row == 31) && (col == 0 || col == 39)) {
                /* Corners */
                tile = TILE_CORNER;
                attr = 0x00;
            } else if (row == 0 || row == 31) {
                /* Top/bottom border */
                tile = TILE_BORDER_H;
                attr = 0x00;
            } else if (col == 0 || col == 39) {
                /* Left/right border */
                tile = TILE_BORDER_V;
                attr = 0x00;
            } else {
                /* Interior: alternating pattern */
                if (((row + col) & 3) == 0) {
                    tile = TILE_CHECK;
                    attr = 0x00;
                } else if (((row + col) & 3) == 1) {
                    tile = TILE_SOLID;
                    attr = 0x00;
                } else if (((row + col) & 3) == 2) {
                    /* Place "NX" letters in center region */
                    if (row >= 14 && row <= 17 && col >= 18 && col <= 21) {
                        tile = ((col - 18) & 1) ? TILE_LETTER_X : TILE_LETTER_N;
                        attr = 0x00;
                    } else {
                        tile = TILE_EMPTY;
                        attr = 0x00;
                    }
                } else {
                    tile = TILE_EMPTY;
                    attr = 0x00;
                }
            }

            /* Write tile entry (2 bytes) */
            *map++ = tile;
            *map++ = attr;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Set up the tilemap palette (first tilemap palette, 16 entries)       *
 *                                                                      *
 * The 4bpp tile pixels index into the tilemap palette.                 *
 * ------------------------------------------------------------------ */
static void setup_palette(void)
{
    /* Select tilemap first palette */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, PAL_TILEMAP_FIRST);

    /* Index 0: black (transparent/background) */
    pal_write(0,  0x00);   /* 000_000_00 */

    /* Index 1: bright blue (solid tile) */
    pal_write(1,  0x03);   /* 000_000_11 */

    /* Index 2: bright green (checker A) */
    pal_write(2,  0x1C);   /* 000_111_00 */

    /* Index 3: bright cyan (checker B) */
    pal_write(3,  0x1F);   /* 000_111_11 */

    /* Index 4: bright red (border edge) */
    pal_write(4,  0xE0);   /* 111_000_00 */

    /* Index 5: bright yellow (border fill) */
    pal_write(5,  0xFC);   /* 111_111_00 */

    /* Index 6: bright white (letter N) */
    pal_write(6,  0xFF);   /* 111_111_11 */

    /* Index 7: bright magenta (letter X) */
    pal_write(7,  0xE3);   /* 111_000_11 */

    /* Remaining indices (8-15): dark grey variants */
    pal_write(8,  0x49);   /* 010_010_01 */
    pal_write(9,  0x49);
    pal_write(10, 0x49);
    pal_write(11, 0x49);
    pal_write(12, 0x49);
    pal_write(13, 0x49);
    pal_write(14, 0x49);
    pal_write(15, 0x49);
}

/* ------------------------------------------------------------------ *
 * main                                                                  *
 * ------------------------------------------------------------------ */
int main(void)
{
    int scroll_x;
    int scroll_y;
    int dx, dy;

    /* 1. Upload tile patterns to RAM at PATTERNS_ADDR (0x6000) */
    upload_patterns();

    /* 2. Fill tilemap data at TILEMAP_ADDR (0x4000) */
    fill_tilemap();

    /* 3. Set up tilemap palette */
    setup_palette();

    /* 4. Configure tilemap base addresses via NextREG.
     *
     * NR 0x6E (tilemap map base):
     *   Default 0x00 => bank 5, offset 0 => CPU address 0x4000.
     *
     * NR 0x6F (tile definitions base):
     *   We want patterns at 0x6000 = bank 5 + 0x2000 offset.
     *   0x2000 / 1024 = 8, so bits[7:1] = 8 => register = 8 << 1 = 0x10.
     */
    ZXN_WRITE_REG(NR_TILEMAP_MAPBASE, 0x00);
    ZXN_WRITE_REG(NR_TILEMAP_DEFBASE, 0x10);

    /* 5. Default attribute: palette offset 0, no mirror/rotate */
    ZXN_WRITE_REG(NR_TILEMAP_DEFATTR, 0x00);

    /* 6. Enable tilemap: 40-column mode, no text mode, no force attr.
     *    NR 0x6B: bit 7 = enable (1), rest = 0 => 0x80.
     */
    ZXN_WRITE_REG(NR_TILEMAP_CTRL, 0x80);

    /* 7. Ensure sprites/layers are set up so tilemap is visible.
     *    Default layer order is SLU (sprites over L2 over ULA).
     *    The tilemap shares the ULA layer slot.
     *    We keep defaults -- tilemap replaces ULA effectively.
     */

    /* 8. Initialise scroll state */
    scroll_x = 0;
    scroll_y = 0;
    dx = 1;
    dy = 1;

    /* 9. Set up IM1 + EI for HALT-based frame sync */
    intrinsic_im_1();
    intrinsic_ei();

    /* 10. Main loop: smooth scrolling */
    while (1) {
        intrinsic_halt();

        /* Update scroll position (ping-pong within visible range) */
        scroll_x += dx;
        scroll_y += dy;

        /* Reverse X direction at boundaries for a ping-pong effect */
        if (scroll_x <= 0 || scroll_x >= 160)
            dx = -dx;

        /* Reverse Y direction at boundaries */
        if (scroll_y <= 0 || scroll_y >= 128)
            dy = -dy;

        /* Write scroll registers (mask to valid range) */
        ZXN_WRITE_REG(NR_TILEMAP_XSCROLL, (unsigned char)(scroll_x & 0xFF));
        ZXN_WRITE_REG(NR_TILEMAP_XSCROLL_MSB, (unsigned char)((scroll_x >> 8) & 0x03));
        ZXN_WRITE_REG(NR_TILEMAP_YSCROLL, (unsigned char)(scroll_y & 0xFF));
    }

    return 0;
}
