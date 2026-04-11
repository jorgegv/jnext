/*
 * Sprite Anchoring (Composite/Relative Sprites) Test
 *
 * Tests the anchor/relative sprite system:
 *   - Anchor type 0: relative inherits position only
 *   - Anchor type 1: relative also inherits mirror/rotate/scale
 *   - Scaled offsets
 *   - Pattern offset mode
 *
 * Extended attribute byte 4 encoding for anchor vs relative:
 *   For anchor:  bit 7 = H4BIT, bit 6 = N6, bit 5 = rel_type (0=type0, 1=type1)
 *   For relative: bits 7:6 = "01" marks this as a relative sprite
 *                 bit 5 = N6, bit 0 = pattern_add (1=add anchor pattern)
 *
 * For relative sprites, attr2 bit 0 is repurposed:
 *   0 = use own palette offset directly
 *   1 = add own palette offset to anchor's palette offset
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       sprite_anchor_test.c -o sprite_anchor_test -create-app
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

/* Upload a 16x16 8bpp pattern with a distinctive design.
 * pattern_id: 0=top-left quadrant, 1=top-right, 2=bottom-left, 3=bottom-right
 * colour: base colour for the pattern */
static void upload_quadrant_pattern(unsigned char slot, unsigned char pattern_id,
                                     unsigned char colour) {
    unsigned int i;
    sprite_slot = slot;
    for (i = 0; i < 256; i++) {
        unsigned char x = i & 0x0F;
        unsigned char y = (i >> 4) & 0x0F;
        unsigned char c = 0;  /* transparent */

        /* Draw a filled square with border */
        if (x == 0 || x == 15 || y == 0 || y == 15) {
            c = 0xFF;  /* white border */
        } else {
            c = colour;
        }

        /* Add a small marker in the corner matching the quadrant */
        if (pattern_id == 0 && x < 4 && y < 4) c = 0xE0;  /* red top-left */
        if (pattern_id == 1 && x > 11 && y < 4) c = 0x1C;  /* green top-right */
        if (pattern_id == 2 && x < 4 && y > 11) c = 0x03;  /* blue bottom-left */
        if (pattern_id == 3 && x > 11 && y > 11) c = 0xFC;  /* yellow bottom-right */

        sprite_pattern = c;
    }
}

/* Upload a simple cross pattern */
static void upload_cross_pattern(unsigned char slot, unsigned char colour) {
    unsigned int i;
    sprite_slot = slot;
    for (i = 0; i < 256; i++) {
        unsigned char x = i & 0x0F;
        unsigned char y = (i >> 4) & 0x0F;
        unsigned char c = 0;  /* transparent */

        if (x >= 6 && x <= 9) c = colour;
        if (y >= 6 && y <= 9) c = colour;
        if (x == 0 || x == 15 || y == 0 || y == 15) c = 0xFF;

        sprite_pattern = c;
    }
}

/* Set a standalone sprite (4-byte format, no extended) */
static void set_sprite_simple(unsigned char slot, unsigned int x, unsigned char y,
                               unsigned char pattern, unsigned char visible) {
    sprite_slot = slot;
    sprite_attr = (unsigned char)(x & 0xFF);
    sprite_attr = y;
    sprite_attr = (unsigned char)((x >> 8) & 0x01);
    sprite_attr = (unsigned char)((visible ? 0x80 : 0x00) | (pattern & 0x3F));
    /* No bit 6 set = no 5th byte = not extended */
}

/* Set an anchor sprite (5-byte extended format).
 * rel_type: 0=type0 (no transform inheritance), 1=type1 (inherit transforms)
 * scale_x/scale_y: 0=1x, 1=2x, 2=4x, 3=8x
 * mirror_x, mirror_y, rotate: transform flags */
static void set_anchor(unsigned char slot, unsigned int x, unsigned char y,
                       unsigned char pattern, unsigned char rel_type,
                       unsigned char scale_x, unsigned char scale_y,
                       unsigned char mirror_x, unsigned char mirror_y,
                       unsigned char rotate, unsigned char pal_offset) {
    sprite_slot = slot;
    /* Byte 0: X low */
    sprite_attr = (unsigned char)(x & 0xFF);
    /* Byte 1: Y */
    sprite_attr = y;
    /* Byte 2: pal_offset(7:4), xmirror(3), ymirror(2), rotate(1), x_msb(0) */
    sprite_attr = (unsigned char)((pal_offset << 4) |
                                   (mirror_x ? 0x08 : 0) |
                                   (mirror_y ? 0x04 : 0) |
                                   (rotate ? 0x02 : 0) |
                                   ((x >> 8) & 0x01));
    /* Byte 3: visible(7)=1, extended(6)=1, pattern(5:0) */
    sprite_attr = (unsigned char)(0xC0 | (pattern & 0x3F));
    /* Byte 4: h4bit(7)=0, n6(6)=0, rel_type(5), xscale(4:3), yscale(2:1), y_msb(0)=0 */
    sprite_attr = (unsigned char)((rel_type ? 0x20 : 0) |
                                   (scale_x << 3) | (scale_y << 1));
}

/* Set a relative sprite (5-byte extended format).
 * x_off, y_off: signed 8-bit offsets from anchor
 * pattern: relative pattern (optionally added to anchor pattern if pattern_add=1)
 * pattern_add: 1 = add anchor's pattern to this sprite's pattern
 * pal_add: 1 = add anchor's palette offset to this sprite's palette offset
 * mirror_x, mirror_y, rotate: transform flags (XOR'd with anchor for type 1) */
static void set_relative(unsigned char slot,
                          unsigned char x_off, unsigned char y_off,
                          unsigned char pattern,
                          unsigned char pal_offset,
                          unsigned char pal_add,
                          unsigned char pattern_add,
                          unsigned char mirror_x, unsigned char mirror_y,
                          unsigned char rotate) {
    sprite_slot = slot;
    /* Byte 0: X offset (signed 8-bit) */
    sprite_attr = x_off;
    /* Byte 1: Y offset (signed 8-bit) */
    sprite_attr = y_off;
    /* Byte 2: pal_offset(7:4), xmirror(3), ymirror(2), rotate(1), pal_add(0) */
    sprite_attr = (unsigned char)((pal_offset << 4) |
                                   (mirror_x ? 0x08 : 0) |
                                   (mirror_y ? 0x04 : 0) |
                                   (rotate ? 0x02 : 0) |
                                   (pal_add ? 0x01 : 0));
    /* Byte 3: visible(7)=1, extended(6)=1, pattern(5:0) */
    sprite_attr = (unsigned char)(0xC0 | (pattern & 0x3F));
    /* Byte 4: bits 7:6 = "01" (relative marker), n6(5)=0, xscale(4:3)=0, yscale(2:1)=0, pattern_add(0) */
    sprite_attr = (unsigned char)(0x40 | (pattern_add ? 0x01 : 0));
}

int main(void) {
    intrinsic_di();

    /* Enable sprites */
    nr_write(0x15, 0x01);

    /* Set sprite transparency index to 0 */
    nr_write(0x4B, 0x00);

    /* Clear ULA screen */
    memset((void *)0x4000, 0, 6144);
    memset((void *)0x5800, 0x08, 768);  /* blue paper */

    /* Upload patterns:
     * Slot 0: TL quadrant (red marker)
     * Slot 1: TR quadrant (green marker)
     * Slot 2: BL quadrant (blue marker)
     * Slot 3: BR quadrant (yellow marker)
     * Slot 4: cross pattern (for standalone reference) */
    upload_quadrant_pattern(0, 0, 0x48);  /* grey-ish */
    upload_quadrant_pattern(1, 1, 0x48);
    upload_quadrant_pattern(2, 2, 0x48);
    upload_quadrant_pattern(3, 3, 0x48);
    upload_cross_pattern(4, 0xE0);  /* red cross */

    /* === GROUP 1: Anchor Type 0 composite (2x2 = 32x32) ===
     * Anchor at (40, 40) with 4 quadrant patterns.
     * Type 0: relatives keep their own mirror/rotate/scale. */

    /* Sprite 0: Anchor type 0 at (40, 40), pattern 0, no transforms */
    set_anchor(0, 40, 40, 0, 0, 0, 0, 0, 0, 0, 0);

    /* Sprite 1: Relative, offset (16, 0) = right of anchor, pattern 1 */
    set_relative(1, 16, 0, 1, 0, 0, 0, 0, 0, 0);

    /* Sprite 2: Relative, offset (0, 16) = below anchor, pattern 2 */
    set_relative(2, 0, 16, 2, 0, 0, 0, 0, 0, 0);

    /* Sprite 3: Relative, offset (16, 16) = diag from anchor, pattern 3 */
    set_relative(3, 16, 16, 3, 0, 0, 0, 0, 0, 0);

    /* === GROUP 2: Anchor Type 1 with X mirror ===
     * Anchor at (120, 40) with X mirror.
     * Type 1: relatives inherit the X mirror transform.
     * The 2x2 should appear horizontally flipped as a group. */

    /* Sprite 4: Anchor type 1 at (120, 40), pattern 0, X mirror */
    set_anchor(4, 120, 40, 0, 1, 0, 0, 1, 0, 0, 0);

    /* Sprite 5-7: Same relative offsets as group 1 */
    set_relative(5, 16, 0, 1, 0, 0, 0, 0, 0, 0);
    set_relative(6, 0, 16, 2, 0, 0, 0, 0, 0, 0);
    set_relative(7, 16, 16, 3, 0, 0, 0, 0, 0, 0);

    /* === GROUP 3: Anchor Type 1 with 2x scale ===
     * Anchor at (40, 110), 2x scale.
     * Type 1: relatives inherit scale, offsets are doubled. */

    /* Sprite 8: Anchor type 1 at (40, 110), pattern 0, 2x scale */
    set_anchor(8, 40, 110, 0, 1, 1, 1, 0, 0, 0, 0);

    /* Sprites 9-11: Relative with same offsets — should be doubled by 2x scale */
    set_relative(9,  16, 0,  1, 0, 0, 0, 0, 0, 0);
    set_relative(10, 0,  16, 2, 0, 0, 0, 0, 0, 0);
    set_relative(11, 16, 16, 3, 0, 0, 0, 0, 0, 0);

    /* === GROUP 4: Pattern offset mode ===
     * Anchor at (200, 40), pattern 0.
     * Relatives use pattern_add=1 so their pattern index is added to anchor's. */

    /* Sprite 12: Anchor type 0 at (200, 40), pattern 0 */
    set_anchor(12, 200, 40, 0, 0, 0, 0, 0, 0, 0, 0);

    /* Sprite 13: Relative, offset (16,0), pattern=1, pattern_add=1 → effective pattern 0+1=1 */
    set_relative(13, 16, 0, 1, 0, 0, 1, 0, 0, 0);

    /* Sprite 14: Relative, offset (0,16), pattern=2, pattern_add=1 → effective pattern 0+2=2 */
    set_relative(14, 0, 16, 2, 0, 0, 1, 0, 0, 0);

    /* Sprite 15: Relative, offset (16,16), pattern=3, pattern_add=1 → effective pattern 0+3=3 */
    set_relative(15, 16, 16, 3, 0, 0, 1, 0, 0, 0);

    /* === GROUP 5: Standalone reference (red cross) ===
     * Simple non-extended sprite to verify chain doesn't leak */
    set_sprite_simple(16, 200, 130, 4, 1);

    /* Infinite loop */
    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
