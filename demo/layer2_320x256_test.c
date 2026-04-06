/*
 * Layer 2 320x256 Mode Test (8bpp, column-major)
 *
 * Enables Layer 2 in 320x256 resolution.  Each pixel is 8 bits (256 colours).
 * Memory layout is column-major: address = x * 256 + y.
 * Total = 320 * 256 = 81920 bytes = 5 x 16K banks (banks 8-12, pages 16-25).
 *
 * Draws vertical colour bands (each column = one colour) so the column-major
 * layout is clearly visible.
 *
 * Expected result: 320 vertical stripes cycling through palette colours,
 * covering the entire 320x256 screen including border area.
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       layer2_320x256_test.c -o layer2_320x256_test -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

__sfr __banked __at 0x243B nextreg_select;
__sfr __banked __at 0x253B nextreg_data;

static void nr_write(unsigned char reg, unsigned char val) {
    nextreg_select = reg;
    nextreg_data = val;
}

int main(void) {
    unsigned char page;

    intrinsic_di();

    /* Set Layer 2 resolution to 320x256 8bpp (NextREG 0x70 bits 5:4 = 01) */
    nr_write(0x70, 0x10);

    /* Layer 2 bank = 8 (default), pages 16-25 */
    nr_write(0x12, 8);

    /* Enable Layer 2 (NextREG 0x69 bit 7) */
    nr_write(0x69, 0x80);

    /* Layer priority: Layer 2 on top */
    nr_write(0x15, 0x00);

    /* Fill L2 memory using MMU slots 2 and 3 (0x4000-0x7FFF = 16K window).
     * We map pairs of 8K pages and fill 16K at a time.
     * Column-major: addr = col * 256 + row.
     * Each column of 256 bytes gets colour = (col & 0xFF).
     *
     * Since columns are 256 bytes and pages are 8192 bytes,
     * each page holds 32 complete columns.
     */
    for (page = 0; page < 10; page += 2) {
        unsigned int col_start, col;
        unsigned char *base;

        /* Map two consecutive 8K pages into slots 2-3 */
        nr_write(0x52, 16 + page);
        nr_write(0x53, 16 + page + 1);

        base = (unsigned char *)0x4000;
        col_start = (unsigned int)(page) * 32;  /* 32 columns per 8K page */

        /* Fill 64 columns (16K = 64 * 256 bytes).
         * Use bright RRRGGGBB colours: cycle R,G,B bands via column index.
         * Colour = column * 4 (stretch to cover brighter range).
         * Each column is a uniform vertical stripe of one colour.
         */
        for (col = 0; col < 64 && (col_start + col) < 320; col++) {
            unsigned int idx = col_start + col;
            /* Create visible colour bands: R cycles every ~10 cols,
             * G cycles independently, B fills in.  This gives a
             * rainbow-like pattern across 320 columns. */
            unsigned char r = (unsigned char)((idx * 7 / 10) & 0x07);  /* 3-bit R */
            unsigned char g = (unsigned char)((idx * 5 / 10) & 0x07);  /* 3-bit G */
            unsigned char b = (unsigned char)((idx * 3 / 10) & 0x03);  /* 2-bit B */
            unsigned char colour = (r << 5) | (g << 2) | b;
            memset(base + col * 256, colour, 256);
        }
    }

    /* Restore MMU slots 2-3 to defaults (pages 10, 11 = bank 5) */
    nr_write(0x52, 10);
    nr_write(0x53, 11);

    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
