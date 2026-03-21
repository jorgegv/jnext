/*
 * ZX Spectrum Next -- Copper co-processor rainbow demo
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 -subtype=nex copper_demo.c -o copper_demo -create-app
 *
 * Produces copper_demo.nex loadable in emulator or real hardware.
 *
 * The Copper changes the fallback colour (NextREG 0x4A) at different
 * scanline positions, creating a rainbow gradient across the active
 * display area.  Layer 2 is enabled and filled with the transparent
 * index (0xE3) so the fallback colour is visible everywhere.
 *
 * The CPU does nothing except HALT in a loop -- all visual work is
 * done by the Copper co-processor.
 *
 * Copper instruction encoding (from VHDL device/copper.vhd):
 *
 *   WAIT:  bit[15]=1, bits[14:9]=hpos (6b), bits[8:0]=vpos (9b)
 *          MSB = 0x80 | (hpos << 1) | (vpos >> 8)
 *          LSB = vpos & 0xFF
 *
 *   MOVE:  bit[15]=0, bits[14:8]=nextreg (7b), bits[7:0]=value
 *          MSB = nextreg & 0x7F
 *          LSB = value
 *
 *   HALT:  0xFF, 0xFF  (WAIT with vpos=511, never matches)
 *   NOP:   0x00, 0x00  (MOVE with nextreg=0, no write pulse)
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* ------------------------------------------------------------------ *
 * NextREG indices                                                      *
 * ------------------------------------------------------------------ */

#define NR_FALLBACK_COL  0x4A   /* fallback colour (8-bit RRRGGGBB)    */
#define NR_DISPLAY_CTRL  0x69   /* display control                     */
#define NR_LAYER2_RES    0x70   /* Layer 2 resolution + palette offset */
#define NR_COPPER_DATA   0x60   /* copper data (MSB/LSB alternating)   */
#define NR_COPPER_ADDR_L 0x61   /* copper write address low 8 bits     */
#define NR_COPPER_CTRL   0x62   /* copper control + addr high bits     */

/* Copper control mode (bits [7:6] of NR 0x62) */
#define COPPER_STOP      0x00   /* 00: stop                            */
#define COPPER_FRAME     0xC0   /* 11: reset PC to 0 each frame        */

/* 16-bit-decoded port -- must use OUT (C),A via __sfr __banked */
__sfr __banked __at 0x123B IO_LAYER2;

/* 8-bit-decoded port */
__sfr __at 0xFE IO_ULA;

/* ------------------------------------------------------------------ *
 * Copper instruction helpers                                           *
 * ------------------------------------------------------------------ */

static void copper_wait(unsigned int vpos, unsigned char hpos)
{
    unsigned char msb, lsb;

    msb = 0x80 | ((hpos & 0x3F) << 1) | ((unsigned char)(vpos >> 8) & 0x01);
    lsb = (unsigned char)(vpos & 0xFF);

    ZXN_WRITE_REG(NR_COPPER_DATA, msb);
    ZXN_WRITE_REG(NR_COPPER_DATA, lsb);
}

static void copper_move(unsigned char nextreg, unsigned char value)
{
    ZXN_WRITE_REG(NR_COPPER_DATA, nextreg & 0x7F);
    ZXN_WRITE_REG(NR_COPPER_DATA, value);
}

static void copper_halt(void)
{
    ZXN_WRITE_REG(NR_COPPER_DATA, 0xFF);
    ZXN_WRITE_REG(NR_COPPER_DATA, 0xFF);
}

/* ------------------------------------------------------------------ *
 * Rainbow colour table (32 entries, RRRGGGBB format)                   *
 * ------------------------------------------------------------------ */

static const unsigned char rainbow[32] = {
    0xE0, 0xE4, 0xE8, 0xEC,   /* red -> orange       */
    0xF0, 0xF4, 0xF8, 0xFC,   /* orange -> yellow    */
    0xDC, 0xBC, 0x9C, 0x7C,   /* yellow -> green     */
    0x5C, 0x3C, 0x3D, 0x3E,   /* green -> cyan       */
    0x3F, 0x1F, 0x1B, 0x17,   /* cyan -> blue        */
    0x13, 0x03, 0x43, 0x83,   /* blue -> magenta     */
    0xC3, 0xE3, 0xE2, 0xE1,   /* magenta -> red      */
    0xE0, 0xC0, 0xA0, 0x80,   /* red -> dark red     */
};

/* ------------------------------------------------------------------ *
 * main                                                                 *
 * ------------------------------------------------------------------ */

int main(void)
{
    unsigned char i;
    unsigned char *p;
    unsigned int j;

    /* 1. Stop the copper and set write address to 0. */
    ZXN_WRITE_REG(NR_COPPER_CTRL, COPPER_STOP);
    ZXN_WRITE_REG(NR_COPPER_ADDR_L, 0x00);

    /* 2. Write copper program: 32 bands of 8 scanlines each.
     *    Each band: WAIT for scanline, MOVE fallback colour.
     *    Total: 32*2 + 1 (HALT) = 65 instructions. */

    for (i = 0; i < 32; i++) {
        copper_wait((unsigned int)i * 8, 0);
        copper_move(NR_FALLBACK_COL, rainbow[i]);
    }
    copper_halt();

    /* 3. Enable Layer 2 (256x192, 8bpp) and fill with transparent (0xE3)
     *    so the fallback colour is visible across the active display. */

    ZXN_WRITE_REG(NR_DISPLAY_CTRL, 0x80);  /* bit 7 = Layer 2 enable */
    ZXN_WRITE_REG(NR_LAYER2_RES, 0x00);

    /* Map all 3 L2 banks at 0x0000-0xBFFF for writing */
    IO_LAYER2 = 0xC3;   /* segment=11, write=1, display=1 */

    p = (unsigned char *)0x0000;
    for (j = 0; j < 0xC000U; j++)
        p[j] = 0xE3;    /* transparent index */

    IO_LAYER2 = 0x02;   /* display=1, disable write-map */

    /* 4. Black ULA border for contrast. */
    IO_ULA = 0x00;

    /* 5. Start copper: mode 11 = reset PC to 0 at each frame. */
    ZXN_WRITE_REG(NR_COPPER_CTRL, COPPER_FRAME);

    /* 6. IM1 + EI for HALT frame sync.  CPU does nothing. */
    intrinsic_im_1();
    intrinsic_ei();

    while (1) {
        intrinsic_halt();
    }

    return 0;
}
