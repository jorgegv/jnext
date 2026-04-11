/*
 * Magic Breakpoint Demo
 *
 * Demonstrates the magic breakpoint feature. When --magic-breakpoint is
 * enabled, executing ED FF (ZEsarUX style) or DD 01 (CSpect style)
 * triggers the debugger.
 *
 * On real hardware (or without --magic-breakpoint), these opcodes behave
 * as NOPs and the program continues normally.
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       magic_bp_demo.c -o magic_bp_demo -subtype=nex -create-app
 *
 * Run:
 *   ./build/jnext --magic-breakpoint --load demo/magic_bp_demo.nex
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* ROM charset helper — same as other demos */
#define ROM_CHARSET 0x3C00

static void print_char(unsigned char col, unsigned char row, unsigned char ch)
{
    unsigned char *screen;
    const unsigned char *font;
    unsigned char i;

    font = (const unsigned char *)(ROM_CHARSET + ((unsigned int)ch << 3));

    for (i = 0; i < 8; i++) {
        screen = (unsigned char *)(0x4000u
                 | ((unsigned int)(row & 0x18) << 8)
                 | ((unsigned int)(i & 7) << 8)
                 | ((unsigned int)(row & 7) << 5)
                 | col);
        *screen = font[i];
    }
}

static void print_str(unsigned char col, unsigned char row, const char *s)
{
    while (*s) {
        print_char(col++, row, *s++);
        if (col >= 32) { col = 0; row++; }
    }
}

/* Magic breakpoint macros */
#define MAGIC_BP_ZESARUX()  __asm__("defb 0xED, 0xFF")
#define MAGIC_BP_CSPECT()   __asm__("defb 0xDD, 0x01")

void main(void)
{
    /* Blue border, clear screen */
    zx_border(1);

    /* Set attributes: white ink, blue paper (0x39) */
    {
        unsigned char *attr = (unsigned char *)0x5800;
        unsigned int i;
        for (i = 0; i < 768; i++) attr[i] = 0x39;
    }

    /* Clear pixel area */
    {
        unsigned char *px = (unsigned char *)0x4000;
        unsigned int i;
        for (i = 0; i < 6144; i++) px[i] = 0;
    }

    print_str(4, 2, "MAGIC BREAKPOINT DEMO");
    print_str(1, 5, "BP 1: ED FF (ZEsarUX)...");

    /* Magic breakpoint 1 — ZEsarUX style */
    MAGIC_BP_ZESARUX();

    print_str(1, 7, "...resumed from BP 1.");
    print_str(1, 9, "BP 2: DD 01 (CSpect)...");

    /* Magic breakpoint 2 — CSpect style */
    MAGIC_BP_CSPECT();

    print_str(1, 11, "...resumed from BP 2.");
    print_str(1, 13, "All breakpoints done!");

    for (;;) {
        intrinsic_halt();
    }
}
