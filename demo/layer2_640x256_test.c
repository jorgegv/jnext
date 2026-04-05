/*
 * Layer 2 640x256 Mode Test
 *
 * Enables Layer 2 in 640x256 resolution (1bpp, 2 colours per pixel).
 * Draws a checkerboard pattern to verify the hi-res mode works.
 *
 * NextREG configuration:
 *   0x70: Layer 2 resolution = 10 (640x256, 1bpp)
 *   0x12: Layer 2 active bank (default 8)
 *   0x69: Display control 1 — enable Layer 2
 *
 * Memory layout for 640x256 1bpp:
 *   Each line = 80 bytes (640 pixels * 1 bit)
 *   Total = 80 * 256 = 20480 bytes = 2.5 x 8K pages
 *   Pages start at bank 8 (pages 16-18)
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       layer2_640x256_test.c -o layer2_640_test -create-app
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
    unsigned char page, *ptr;
    unsigned int line;

    intrinsic_di();

    /* Set Layer 2 resolution to 640x256 1bpp (NextREG 0x70 bits 5:4 = 10) */
    nr_write(0x70, 0x20);

    /* Layer 2 bank = 8 (default) */
    nr_write(0x12, 8);

    /* Enable Layer 2 (NextREG 0x69 bit 7) */
    nr_write(0x69, 0x80);

    /* Set layer priority */
    nr_write(0x15, 0x00);

    /* Draw checkerboard: alternate 0xAA and 0x55 per line.
     * In 1bpp mode, each byte = 8 pixels (MSB = leftmost).
     * 80 bytes per line.
     */
    for (line = 0; line < 256; line++) {
        unsigned int byte_offset = line * 80;
        page = (unsigned char)(byte_offset >> 13);
        unsigned int page_offset = byte_offset & 0x1FFF;

        nr_write(0x52, 16 + page);

        ptr = (unsigned char *)(0x4000 + page_offset);

        unsigned char pattern = (line & 1) ? 0xAA : 0x55;
        memset(ptr, pattern, 80);
    }

    /* Restore MMU slot 2 */
    nr_write(0x52, 10);

    /* Infinite loop */
    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
