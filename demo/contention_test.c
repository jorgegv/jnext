/*
 * Memory Contention Test
 *
 * Measures the time to execute a tight loop reading from contended memory
 * (0x4000) vs uncontended memory (0x8000). On 48K/128K, reads from
 * 0x4000-0x7FFF are slowed by ULA contention during active display.
 * On Pentagon, there is no contention — both should be equal.
 *
 * The test counts iterations of a tight loop between two interrupts
 * (one frame = ~70000 T-states) for both contended and uncontended
 * addresses, then displays the results.
 *
 * Expected results:
 *   48K/128K: contended count < uncontended count (contention slows reads)
 *   Pentagon:  contended count ≈ uncontended count (no contention)
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       contention_test.c -o contention_test -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

__sfr __at 0xFE ula_port;

/* Volatile to prevent optimizer from removing the reads */
static volatile unsigned char dummy;

/* Count loop iterations reading from a given address for one frame.
 * Uses inline asm for a tight loop to maximize sensitivity. */
static unsigned int count_reads(unsigned char *addr) __naked {
    (void)addr;
    __asm
        ei
        halt
        ld      de, #0
    00101$:
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        inc     de
        ld      a, d
        cp      #0x10
        jr      c, 00101$
        ex      de, hl
        ret
    __endasm;
}

/* Simple decimal print: 5-digit number at screen position */
static void print_number(unsigned char *attrs, unsigned int val) {
    unsigned char digits[5];
    int i;
    for (i = 4; i >= 0; i--) {
        digits[i] = val % 10;
        val /= 10;
    }
    /* Use attribute colours to display digits: ink = digit value */
    for (i = 0; i < 5; i++) {
        /* bright + paper black + ink = digit: gives visible colour per digit */
        attrs[i] = 0x40 | (digits[i] & 7);
        if (digits[i] >= 8) attrs[i] |= 0x08;  /* bright for 8-9 */
    }
}

/* Print a text string using pixel data (1-bit font from ROM) */
static void print_text(unsigned int screen_addr, const char *text) {
    unsigned char *font = (unsigned char *)0x3D00;  /* 48K ROM charset at 0x3D00 */
    unsigned char *scr = (unsigned char *)screen_addr;
    while (*text) {
        unsigned char ch = *text - 32;
        unsigned char *glyph = font + ch * 8;
        int row;
        for (row = 0; row < 8; row++) {
            /* Calculate screen address for each pixel row */
            unsigned int addr = screen_addr + (row << 8);
            *((unsigned char *)addr) = glyph[row];
            addr++;
        }
        screen_addr++;
        text++;
    }
}

int main(void) {
    unsigned int count_contended, count_uncontended;
    unsigned char *attrs;
    int diff;

    /* Clear screen */
    memset((void *)0x4000, 0, 6144);
    memset((void *)0x5800, 0x38, 768);  /* white ink, black paper */

    /* Measure contended memory reads (0x4000 = screen memory) */
    count_contended = count_reads((unsigned char *)0x4000);

    /* Measure uncontended memory reads (0x8000 = normal RAM) */
    count_uncontended = count_reads((unsigned char *)0x8000);

    /* Display results using attribute colours on specific lines:
     * Line 10: contended count
     * Line 12: uncontended count
     * Line 14: difference indicator
     */
    attrs = (unsigned char *)0x5800 + 10 * 32;
    print_number(attrs, count_contended);
    /* Label: "C:" */
    attrs[6] = 0x47;  /* white ink = "label" */
    attrs[7] = 0x47;

    attrs = (unsigned char *)0x5800 + 12 * 32;
    print_number(attrs, count_uncontended);
    attrs[6] = 0x47;
    attrs[7] = 0x47;

    /* Difference line */
    attrs = (unsigned char *)0x5800 + 14 * 32;
    diff = (int)count_uncontended - (int)count_contended;

    if (diff > 50) {
        /* Significant difference: contention detected (48K/128K behavior) */
        /* Show yellow border = contention present */
        ula_port = 6;
        /* Mark attrs yellow */
        memset(attrs, 0x46, 10);  /* yellow = contention detected */
    } else {
        /* No significant difference: no contention (Pentagon behavior) */
        /* Show cyan border = no contention */
        ula_port = 5;
        /* Mark attrs cyan */
        memset(attrs, 0x45, 10);  /* cyan = no contention */
    }

    /* Show the difference as a number too */
    if (diff < 0) diff = -diff;
    print_number(attrs + 12, (unsigned int)diff);

    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
