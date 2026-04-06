/*
 * Layer 2 640x256 Mode Test (4bpp, column-major, 2 pixels per byte)
 *
 * Enables Layer 2 in 640x256 resolution.  Each byte holds 2 pixels:
 * high nibble = left pixel, low nibble = right pixel (4 bits, 16 colours).
 * Memory layout is column-major: address = col * 256 + row (same as 320x256).
 * Total = 320 * 256 = 81920 bytes = 5 x 16K banks.
 *
 * Draws a checkerboard pattern where pixel pairs alternate:
 *   Even (col+row): byte = 0xF0 (left=white, right=black)
 *   Odd  (col+row): byte = 0x0F (left=black, right=white)
 *
 * Expected result: fine checkerboard covering the full screen.
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       layer2_640x256_test.c -o layer2_640x256_test -subtype=nex -create-app
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

    /* Set Layer 2 resolution to 640x256 4bpp (NextREG 0x70 bits 5:4 = 10) */
    nr_write(0x70, 0x20);

    /* Layer 2 bank = 8 (default) */
    nr_write(0x12, 8);

    /* Enable Layer 2 (NextREG 0x69 bit 7) */
    nr_write(0x69, 0x80);

    /* Layer priority: Layer 2 on top */
    nr_write(0x15, 0x00);

    /* Fill L2 memory: column-major, 2 pixels per byte.
     * Checkerboard: alternate 0xF0 / 0x0F based on (col + row) parity.
     * Since columns are 256 bytes, even columns get {0xF0,0x0F,0xF0,...}
     * and odd columns get {0x0F,0xF0,0x0F,...}.
     */
    for (page = 0; page < 10; page += 2) {
        unsigned int col_start, col, row;
        unsigned char *base;

        nr_write(0x52, 16 + page);
        nr_write(0x53, 16 + page + 1);

        base = (unsigned char *)0x4000;
        col_start = (unsigned int)(page) * 32;

        for (col = 0; col < 64 && (col_start + col) < 320; col++) {
            unsigned char *col_ptr = base + col * 256;
            unsigned char even_byte = ((col_start + col) & 1) ? 0x0F : 0xF0;
            unsigned char odd_byte  = ((col_start + col) & 1) ? 0xF0 : 0x0F;

            /* Fill alternating rows */
            for (row = 0; row < 256; row += 2) {
                col_ptr[row]     = even_byte;
                col_ptr[row + 1] = odd_byte;
            }
        }
    }

    /* Restore MMU slots */
    nr_write(0x52, 10);
    nr_write(0x53, 11);

    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
