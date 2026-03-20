/*
 * ZX Spectrum Next – Layer 2 background + bouncing hardware sprite
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
 * Palette helper                                                        *
 * ------------------------------------------------------------------ */
static void pal_write(unsigned char idx, unsigned char rrrgggbb)
{
    ZXN_WRITE_REG(NR_PALETTE_IDX, idx);
    ZXN_WRITE_REG(NR_PALETTE_VAL, rrrgggbb);
}

/* ------------------------------------------------------------------ *
 * Layer 2 fill (256×192 @ 8 bpp = 49152 bytes)                        *
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
 * ------------------------------------------------------------------ */
static void l2_fill(unsigned char colour)
{
    unsigned char *p = (unsigned char *)0x0000;
    unsigned int   i;

    /* segment=11 + write + display = 0xC3 */
    IO_LAYER2 = 0xC3;

    for (i = 0; i < 0xC000U; i++)
        p[i] = colour;

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
 * main                                                                  *
 * ------------------------------------------------------------------ */
int main(void)
{
    /* Sprite coordinates are in absolute framebuffer space (whc domain):
     *   X: 0-31 = left border, 32-287 = display area, 288-319 = right border
     *   Y: 0-31 = top border,  32-223 = display area, 224-255 = bottom border
     * Start roughly centered in the display area. */
    int sx = 152, sy = 120;
    int dx = 1,   dy = 1;

    /* 1. Layer 2: 256×192, palette offset 0. */
    ZXN_WRITE_REG(NR_LAYER2_RES, 0x00);

    /* 2. Sprite transparent index → 0  (default is 0xE3). */
    ZXN_WRITE_REG(NR_SPRITE_TRANSP, 0x00);

    /* 3. Layer 2 palette: index 1 = sky blue (RRRGGGBB 011_110_11). */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, NR_PAL_L2_FIRST);
    pal_write(1, 0x7B);

    /* 4. Sprite palette: index 2 = red, index 4 = yellow. */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, NR_PAL_SPR_FIRST);
    pal_write(2, 0xE0);    /* 111_000_00 = bright red    */
    pal_write(4, 0xFC);    /* 111_111_00 = bright yellow  */

    /* 5. Enable Layer 2 display (NR 0x69 bit 7). */
    ZXN_WRITE_REG(NR_DISPLAY_CTRL, 0x80);

    /* 6. Fill Layer 2 background. */
    l2_fill(1);

    /* 7. Enable sprites globally (NR 0x15 bit 0). */
    ZXN_WRITE_REG(NR_SPRITE_CTRL, 0x01);

    /* 8. Upload sprite pattern to slot 0. */
    sprite_upload_pattern(0, sprite_pat);

    /* 9. Make sprite visible at initial position BEFORE entering the
     *    loop — don't wait for the first interrupt. */
    sprite_set_attr(0, (unsigned int)sx, (unsigned char)sy, 0, 1);

    /* 10. Ensure IM 1 + EI so HALT can return on the frame interrupt. */
    intrinsic_im_1();
    intrinsic_ei();

    /* 11. Main loop. */
    while (1) {
        intrinsic_halt();

        sx += dx;
        sy += dy;

        /* Bounce within display area (32-287 for X, 32-223 for Y),
         * minus sprite size (16 pixels). */
        if (sx <= 32 || sx >= 271) { dx = -dx; sx += dx; }
        if (sy <= 32 || sy >= 207) { dy = -dy; sy += dy; }

        sprite_set_attr(0, (unsigned int)sx, (unsigned char)sy, 0, 1);
    }

    return 0;
}
