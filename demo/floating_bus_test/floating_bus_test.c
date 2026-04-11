/*
 * Floating Bus Test
 *
 * Tests the floating bus behavior: reading from port 0xFF during active
 * display should return the last byte fetched by the ULA from VRAM.
 * Outside active display, it returns 0xFF.
 *
 * The test fills the screen with a known pattern, then reads port 0xFF
 * at various scanline positions and displays the raw hex values.
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       floating_bus_test.c -o floating_bus_test -subtype=nex
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

__sfr __at 0xFE ula_port;
__sfr __at 0xFF floating_bus;

/* Print a hex byte at screen attribute position */
static const unsigned char hex_chars[] = "0123456789ABCDEF";

static void print_hex_at(unsigned char *screen, unsigned char val) {
    /* Simple 1x8 font - just set attribute colours to show values */
    screen[0] = val;
}

static void fill_screen_pattern(void) {
    unsigned char *screen = (unsigned char *)0x4000;
    unsigned char *attrs  = (unsigned char *)0x5800;
    unsigned int i;

    /* Fill pixel area with alternating pattern */
    for (i = 0; i < 6144; i++) {
        screen[i] = (i & 1) ? 0xAA : 0x55;
    }
    /* Fill attributes with distinct values per column */
    for (i = 0; i < 768; i++) {
        attrs[i] = (unsigned char)((i & 0x1F) | 0x40);
    }
}

int main(void) {
    unsigned char reads[32];
    unsigned char non_ff_count = 0;
    unsigned char *attrs;
    unsigned int i, j;

    /* Set border to black */
    ula_port = 0;

    /* Fill screen with known pattern */
    fill_screen_pattern();

    /* Wait for frame start (need interrupts enabled for HALT) */
    intrinsic_ei();
    intrinsic_halt();

    /* Small delay to get into active display area */
    for (i = 0; i < 100; i++) {
        __asm nop __endasm;
    }

    /* Read floating bus 32 times */
    for (i = 0; i < 32; i++) {
        reads[i] = floating_bus;
        for (j = 0; j < 10; j++) {
            __asm nop __endasm;
        }
    }

    /* Clear bottom lines and show results as attribute colours */
    attrs = (unsigned char *)0x5800 + 22 * 32;  /* line 22 */
    memset(attrs, 0x38, 32);  /* white on black */

    /* Write read values into attributes on line 23-24 */
    attrs = (unsigned char *)0x5800 + 23 * 32;
    for (i = 0; i < 32; i++) {
        attrs[i] = reads[i];
        if (reads[i] != 0xFF) non_ff_count++;
    }

    /* Show pass/fail as border colour */
    if (non_ff_count > 0) {
        ula_port = 4;  /* green border = PASS */
    } else {
        ula_port = 2;  /* red border = FAIL */
    }

    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
