/*
 * ZX Spectrum Next – Layer 2 background + 40 bouncing hardware sprites
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       layer2_sprite_demo.c -o demo -create-app
 *
 * Produces demo.nex loadable in CSpect / ZEsarUX / real hardware.
 *
 * Port bit layouts verified against the official FPGA VHDL source:
 *   cores/zxnext/src/zxnext.vhd        (port 0x123B, NextREG wiring)
 *   cores/zxnext/src/video/sprites.vhd (attribute byte format)
 *   cores/zxnext/nextreg.txt           (register descriptions and reset values)
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>   /* ZXN_WRITE_REG() macro                      */
#include <intrinsic.h>  /* intrinsic_halt(), intrinsic_ei()            */

/* ------------------------------------------------------------------ *
 * Port I/O declarations                                                *
 *                                                                      *
 * IMPORTANT: z80_outp() in z88dk/SDCC may use OUT (n),A which only    *
 * puts the low byte on the address bus.  ZX Next ports 0x123B and     *
 * 0x303B decode the FULL 16-bit address, so we MUST use               *
 * __sfr __banked for correct OUT (C),A generation.                    *
 * ------------------------------------------------------------------ */

/* 16-bit-decoded ports – must use OUT (C),A via __sfr __banked */
__sfr __banked __at 0x123B IO_LAYER2;     /* Layer 2 access control   */
__sfr __banked __at 0x303B IO_SPRITE_SLOT;/* Sprite slot select       */

/* 8-bit-decoded ports – only low byte matters, __sfr is fine */
__sfr __at 0x57 IO_SPRITE_ATTR;           /* Sprite attribute (auto++) */
__sfr __at 0x5B IO_SPRITE_PATT;           /* Sprite pattern   (auto++) */

/* ------------------------------------------------------------------ *
 * NextREG indices                                                      *
 * ------------------------------------------------------------------ */

#define NR_DISPLAY_CTRL  0x69   /* bit 7 = Layer 2 enable              */
#define NR_LAYER2_RES    0x70   /* bits[5:4]=res, bits[3:0]=pal offset */
#define NR_SPRITE_CTRL   0x15   /* bit0=enable, bit1=over-border       */
#define NR_PALETTE_CTRL  0x43   /* bits[6:4]=palette select            */
#define NR_PALETTE_IDX   0x40   /* palette entry index                 */
#define NR_PALETTE_VAL   0x41   /* 8-bit RRRGGGBB value                */
#define NR_SPRITE_TRANSP 0x4B   /* sprite transparent index (def=0xE3) */

#define NR_PAL_L2_FIRST  0x10   /* bits[6:4]=001 → Layer2 first pal   */
#define NR_PAL_SPR_FIRST 0x20   /* bits[6:4]=010 → Sprite first pal   */

/* ------------------------------------------------------------------ *
 * Number of sprites                                                    *
 * ------------------------------------------------------------------ */
#define NUM_SPRITES 40

/* ------------------------------------------------------------------ *
 * Simple PRNG (16-bit xorshift)                                        *
 * ------------------------------------------------------------------ */
static unsigned int rng_state = 0xACE1;

static unsigned int rng_next(void)
{
    rng_state ^= rng_state << 7;
    rng_state ^= rng_state >> 9;
    rng_state ^= rng_state << 8;
    return rng_state;
}

/* Return a value in [lo, hi] inclusive. */
static int rng_range(int lo, int hi)
{
    unsigned int range = (unsigned int)(hi - lo + 1);
    return lo + (int)(rng_next() % range);
}

/* ------------------------------------------------------------------ *
 * Palette helper                                                        *
 * ------------------------------------------------------------------ */
static void pal_write(unsigned char idx, unsigned char rrrgggbb)
{
    ZXN_WRITE_REG(NR_PALETTE_IDX, idx);
    ZXN_WRITE_REG(NR_PALETTE_VAL, rrrgggbb);
}

/* ------------------------------------------------------------------ *
 * Layer 2 pattern (256×192 @ 8 bpp = 49152 bytes)                    *
 *                                                                      *
 * Port 0x123B bit layout (from zxnext.vhd):                           *
 *   bit 0 : write-map enable                                          *
 *   bit 1 : display enable                                             *
 *   bit 2 : read-map enable (we leave this OFF so code at 0x8000      *
 *           is still fetched from normal RAM)                          *
 *   bit 3 : shadow select                                              *
 *   bits [7:6] : segment  (11 = map all 3 L2 banks at 0x0000–0xBFFF) *
 *                                                                      *
 * 0xC000–0xFFFF is NOT remapped → stack at 0xFFFD is safe.            *
 *                                                                      *
 * Pattern: diagonal rainbow stripes.  For each pixel at (col, row),   *
 * colour index = ((row + col) >> 5) & 7) + 1.                        *
 * This gives 8-colour bands 32 pixels wide along the NW→SE diagonal. *
 * ------------------------------------------------------------------ */
static void l2_draw_pattern(void)
{
    unsigned char *p = (unsigned char *)0x0000;
    unsigned int   i;
    unsigned char  row, col;

    /* segment=11 + write + display = 0xC3 */
    IO_LAYER2 = 0xC3;

    for (i = 0; i < 0xC000U; i++) {
        row = (unsigned char)(i >> 8);   /* 0–191 */
        col = (unsigned char)(i);        /* 0–255 */
        p[i] = (unsigned char)(((unsigned int)row + col) >> 5 & 7u) + 1;
    }

    /* keep display, disable write-map */
    IO_LAYER2 = 0x02;
}

/* ------------------------------------------------------------------ *
 * Sprite pattern: 16×16, 8 bpp, diamond shape                         *
 *   Index 0  = transparent (we set NR 0x4B = 0 below)                *
 *   Index 2  = bright red    (set in sprite palette)                  *
 *   Index 4  = bright yellow (set in sprite palette)                  *
 * ------------------------------------------------------------------ */
static const unsigned char sprite_pat[256] = {
    /* row  0 */ 0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,
    /* row  1 */ 0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,
    /* row  2 */ 0,0,0,0,0,2,2,2,2,2,2,0,0,0,0,0,
    /* row  3 */ 0,0,0,0,2,2,2,2,2,2,2,2,0,0,0,0,
    /* row  4 */ 0,0,0,2,2,4,4,2,2,4,4,2,2,0,0,0,
    /* row  5 */ 0,0,2,2,4,4,4,2,2,4,4,4,2,2,0,0,
    /* row  6 */ 0,2,2,2,4,4,2,2,2,2,4,4,2,2,2,0,
    /* row  7 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    /* row  8 */ 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    /* row  9 */ 0,2,2,2,4,4,2,2,2,2,4,4,2,2,2,0,
    /* row 10 */ 0,0,2,2,4,4,4,2,2,4,4,4,2,2,0,0,
    /* row 11 */ 0,0,0,2,2,4,4,2,2,4,4,2,2,0,0,0,
    /* row 12 */ 0,0,0,0,2,2,2,2,2,2,2,2,0,0,0,0,
    /* row 13 */ 0,0,0,0,0,2,2,2,2,2,2,0,0,0,0,0,
    /* row 14 */ 0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,
    /* row 15 */ 0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,
};

/* Upload 256 bytes to pattern slot 'slot' (0–63). */
static void sprite_upload_pattern(unsigned char slot,
                                   const unsigned char *data)
{
    unsigned int i;
    IO_SPRITE_SLOT = slot & 0x3F;
    for (i = 0; i < 256; i++)
        IO_SPRITE_PATT = data[i];
}

/* Write 4-byte attributes for sprite 'slot'.
 *
 * Attribute format (from sprites.vhd):
 *   B0: x[7:0]
 *   B1: y[7:0]
 *   B2: [7:4]=palette_offset, [3]=mirrorX, [2]=mirrorY, [1]=rotate, [0]=x_msb
 *   B3: [7]=visible, [6]=5byte(0), [5:0]=pattern_number
 */
static void sprite_set_attr(unsigned char slot,
                             unsigned int  x,
                             unsigned char y,
                             unsigned char pattern,
                             unsigned char visible)
{
    IO_SPRITE_SLOT = slot & 0x7F;
    IO_SPRITE_ATTR = (unsigned char)(x & 0xFF);                /* B0 */
    IO_SPRITE_ATTR = y;                                        /* B1 */
    IO_SPRITE_ATTR = (unsigned char)((x >> 8) & 0x01);        /* B2 */
    IO_SPRITE_ATTR = (unsigned char)(((visible & 1u) << 7)
                                      | (pattern & 0x3Fu));    /* B3 */
}

/* ------------------------------------------------------------------ *
 * Sprite state arrays                                                   *
 * ------------------------------------------------------------------ */
static int sx[NUM_SPRITES], sy[NUM_SPRITES];
static int dx[NUM_SPRITES], dy[NUM_SPRITES];

/* ------------------------------------------------------------------ *
 * main                                                                  *
 * ------------------------------------------------------------------ */
int main(void)
{
    unsigned char i;

    /* 1. Layer 2: 256×192, palette offset 0. */
    ZXN_WRITE_REG(NR_LAYER2_RES, 0x00);

    /* 2. Sprite transparent index → 0  (default is 0xE3). */
    ZXN_WRITE_REG(NR_SPRITE_TRANSP, 0x00);

    /* 3. Layer 2 palette: indices 1–8 = diagonal rainbow stripe colours.
     *    Format: RRRGGGBB (bits [7:5]=R, [4:2]=G, [1:0]=B). */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, NR_PAL_L2_FIRST);
    pal_write(1, 0xED);  /* light red     111 011 01 */
    pal_write(2, 0xF5);  /* light orange  111 101 01 */
    pal_write(3, 0xFD);  /* light yellow  111 111 01 */
    pal_write(4, 0x7D);  /* light green   011 111 01 */
    pal_write(5, 0x3F);  /* light cyan    001 111 11 */
    pal_write(6, 0x6F);  /* light blue    011 011 11 */
    pal_write(7, 0xEF);  /* light magenta 111 011 11 */
    pal_write(8, 0xFF);  /* white         111 111 11 */

    /* 4. Sprite palette: index 2 = red, index 4 = yellow. */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, NR_PAL_SPR_FIRST);
    pal_write(2, 0xE0);    /* 111_000_00 = bright red    */
    pal_write(4, 0xFC);    /* 111_111_00 = bright yellow  */

    /* 5. Enable Layer 2 display (NR 0x69 bit 7). */
    ZXN_WRITE_REG(NR_DISPLAY_CTRL, 0x80);

    /* 6. Draw diagonal rainbow pattern on Layer 2. */
    l2_draw_pattern();

    /* 7. Enable sprites globally (NR 0x15 bit 0). */
    ZXN_WRITE_REG(NR_SPRITE_CTRL, 0x01);

    /* 8. Upload sprite pattern to slot 0. */
    sprite_upload_pattern(0, sprite_pat);

    /* 9. Initialise 40 sprites at random positions with random velocities.
     *    Sprite coordinates are in absolute framebuffer space:
     *      X: 32-271 = display area (minus 16px sprite width)
     *      Y: 32-207 = display area (minus 16px sprite height) */
    for (i = 0; i < NUM_SPRITES; i++) {
        sx[i] = rng_range(32, 271);
        sy[i] = rng_range(32, 207);
        dx[i] = rng_range(1, 4);
        if (rng_next() & 1) dx[i] = -dx[i];
        dy[i] = rng_range(1, 4);
        if (rng_next() & 1) dy[i] = -dy[i];
        sprite_set_attr(i, (unsigned int)sx[i], (unsigned char)sy[i], 0, 1);
    }

    /* 10. Ensure IM 1 + EI so HALT can return on the frame interrupt. */
    intrinsic_im_1();
    intrinsic_ei();

    /* 11. Main loop — update all 40 sprites each frame. */
    while (1) {
        intrinsic_halt();

        for (i = 0; i < NUM_SPRITES; i++) {
            sx[i] += dx[i];
            sy[i] += dy[i];

            /* Bounce within display area. */
            if (sx[i] <= 32 || sx[i] >= 271) { dx[i] = -dx[i]; sx[i] += dx[i]; }
            if (sy[i] <= 32 || sy[i] >= 207) { dy[i] = -dy[i]; sy[i] += dy[i]; }

            sprite_set_attr(i, (unsigned int)sx[i], (unsigned char)sy[i], 0, 1);
        }
    }

    return 0;
}
