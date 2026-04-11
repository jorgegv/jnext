/*
 * Sprite Scaling Test (x1, x2, x4, x8)
 *
 * Displays 4 sprites at different scaling factors to verify the
 * NextREG sprite scaling feature works correctly.
 *
 * Sprite scaling is controlled by NextREG 0x4B bits 4:3:
 *   00 = 1x (16x16)
 *   01 = 2x (32x32)
 *   10 = 4x (64x64)
 *   11 = 8x (128x128)
 *
 * Actually, per-sprite scaling uses the extended attribute byte 4:
 *   bits 4:3 = X scale (00=1x, 01=2x, 10=4x, 11=8x)
 *   bits 2:1 = Y scale (00=1x, 01=2x, 10=4x, 11=8x)
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       sprite_scaling_test.c -o sprite_scale_test -create-app
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

__sfr __banked __at 0x243B nextreg_select;
__sfr __banked __at 0x253B nextreg_data;
__sfr __banked __at 0x303B sprite_slot;
__sfr __at     0x57        sprite_attr;
__sfr __at     0x5B        sprite_pattern;

static void nr_write(unsigned char reg, unsigned char val) {
    nextreg_select = reg;
    nextreg_data = val;
}

/* Upload a 16x16 8bpp sprite pattern (256 bytes) to pattern slot */
static void upload_pattern(unsigned char slot) {
    unsigned int i;
    sprite_slot = slot;  /* Select pattern slot for upload */
    /* Draw a simple cross pattern */
    for (i = 0; i < 256; i++) {
        unsigned char x = i & 0x0F;
        unsigned char y = (i >> 4) & 0x0F;
        unsigned char colour;
        if (x == 7 || x == 8 || y == 7 || y == 8) {
            colour = 0xE0;  /* Red (RGB332) */
        } else if (x == 0 || x == 15 || y == 0 || y == 15) {
            colour = 0x1C;  /* Green border */
        } else {
            colour = 0xE0;  /* Transparent (index 0xE0 = visible colour) */
        }
        /* Use bright colours for visibility */
        if (x == 7 || x == 8 || y == 7 || y == 8) {
            colour = 0xFC;  /* Bright yellow */
        } else if (x == 0 || x == 15 || y == 0 || y == 15) {
            colour = 0x03;  /* Blue border */
        } else {
            colour = 0x00;  /* Transparent (index 0) */
        }
        sprite_pattern = colour;
    }
}

/* Set sprite attributes using 5-byte extended format */
static void set_sprite(unsigned char slot, unsigned int x, unsigned char y,
                       unsigned char pattern, unsigned char scale_x, unsigned char scale_y,
                       unsigned char visible) {
    sprite_slot = slot;  /* Select attribute slot */

    /* Byte 0: X low 8 bits */
    sprite_attr = (unsigned char)(x & 0xFF);
    /* Byte 1: Y */
    sprite_attr = y;
    /* Byte 2: X MSB (bit 0), palette offset (bits 7:4), mirror/rotate */
    sprite_attr = (unsigned char)((x >> 8) & 0x01);
    /* Byte 3: visible (bit 7), 5th attr enable (bit 6), pattern (bits 5:0) */
    sprite_attr = (unsigned char)((visible ? 0x80 : 0x00) | 0x40 | (pattern & 0x3F));
    /* Byte 4 (extended): scale_x (bits 4:3), scale_y (bits 2:1) */
    sprite_attr = (unsigned char)((scale_x << 3) | (scale_y << 1));
}

int main(void) {
    intrinsic_di();

    /* Enable sprites (NextREG 0x15 bit 0 = sprites visible) */
    nr_write(0x15, 0x01);

    /* Set sprite transparency index to 0 (NextREG 0x4B) */
    nr_write(0x4B, 0x00);

    /* Clear ULA screen to dark blue */
    memset((void *)0x4000, 0, 6144);
    memset((void *)0x5800, 0x08, 768);  /* blue paper, black ink */

    /* Upload sprite pattern to slot 0 */
    upload_pattern(0);

    /* Place 4 sprites with different scales:
     *   Sprite 0: 1x (16x16) at position (32, 32)
     *   Sprite 1: 2x (32x32) at position (80, 32)
     *   Sprite 2: 4x (64x64) at position (32, 100)
     *   Sprite 3: 8x (128x128) at position (120, 80)
     *
     * Coordinates include the 32-pixel border offset in sprite space.
     */
    set_sprite(0, 32 + 32,  32 + 32,  0, 0, 0, 1);  /* 1x */
    set_sprite(1, 32 + 80,  32 + 32,  0, 1, 1, 1);  /* 2x */
    set_sprite(2, 32 + 32,  32 + 100, 0, 2, 2, 1);  /* 4x */
    set_sprite(3, 32 + 120, 32 + 80,  0, 3, 3, 1);  /* 8x */

    /* Infinite loop */
    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
