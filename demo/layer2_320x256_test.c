/*
 * Layer 2 320x256 Mode Test
 *
 * Enables Layer 2 in 320x256 resolution (4bpp, 16 colours per pixel).
 * Draws a vertical colour gradient to verify the extended resolution
 * mode works correctly.
 *
 * NextREG configuration:
 *   0x70: Layer 2 resolution = 01 (320x256, 4bpp)
 *   0x12: Layer 2 active bank (default 8)
 *   0x69: Display control 1 — enable Layer 2
 *   0x15: Sprite/Layer priority
 *
 * Memory layout for 320x256 4bpp:
 *   Each line = 160 bytes (320 pixels * 4 bits)
 *   Total = 160 * 256 = 40960 bytes = 5 x 8K pages
 *   Pages start at bank 8 (pages 16-20)
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       layer2_320x256_test.c -o layer2_320_test -create-app
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

/* NextREG port access */
__sfr __banked __at 0x243B nextreg_select;
__sfr __banked __at 0x253B nextreg_data;

static void nr_write(unsigned char reg, unsigned char val) {
    nextreg_select = reg;
    nextreg_data = val;
}

int main(void) {
    unsigned char page, y, x;
    unsigned char *ptr;
    unsigned int line;

    intrinsic_di();

    /* Set Layer 2 resolution to 320x256 4bpp (NextREG 0x70 bits 5:4 = 01) */
    nr_write(0x70, 0x10);

    /* Layer 2 bank = 8 (default) */
    nr_write(0x12, 8);

    /* Enable Layer 2 (NextREG 0x69 bit 7) */
    nr_write(0x69, 0x80);

    /* Set layer priority: Layer 2 on top (NextREG 0x15) */
    nr_write(0x15, 0x00);

    /* Draw vertical gradient: each line gets a colour based on Y.
     * In 4bpp mode, each byte contains 2 pixels (high nibble = left).
     * We write 160 bytes per line.
     *
     * Memory is paged via NextREG 0x12 (Layer 2 bank).
     * We use MMU slot 2 (0x4000-0x5FFF) to write to L2 pages.
     */
    for (line = 0; line < 256; line++) {
        /* Calculate which 8K page this line falls in */
        /* 160 bytes/line, 8192 bytes/page = 51.2 lines/page */
        unsigned int byte_offset = line * 160;
        page = (unsigned char)(byte_offset >> 13);  /* byte_offset / 8192 */
        unsigned int page_offset = byte_offset & 0x1FFF;

        /* Map the page into slot 2 (0x4000-0x5FFF) via MMU */
        nr_write(0x52, 16 + page);  /* Page 16 = bank 8 first page */

        ptr = (unsigned char *)(0x4000 + page_offset);

        /* Fill line with gradient colour (Y mod 16 in both nibbles) */
        unsigned char colour = (unsigned char)((line & 0x0F) | ((line & 0x0F) << 4));
        memset(ptr, colour, 160);
    }

    /* Restore MMU slot 2 to default (page 10 = bank 5) */
    nr_write(0x52, 10);

    /* Set border to blue */
    ZXN_WRITE_REG(0x42, 0x01);  /* ULA palette = blue border */

    /* Infinite loop */
    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
