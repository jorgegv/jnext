/*
 * Magic Port Demo
 *
 * Demonstrates the magic debug port feature. When --magic-port is
 * enabled, writing to the configured port outputs debug information
 * to stderr on the host.
 *
 * This demo uses port 0xCAFE as the magic port. Run with:
 *
 *   ./build/jnext --magic-port 0xCAFE --magic-port-mode line \
 *       --load demo/magic_port_demo.nex
 *
 * Try different modes:
 *   --magic-port-mode hex    : output "41", "42", etc.
 *   --magic-port-mode dec    : output "65", "66", etc.
 *   --magic-port-mode ascii  : output raw characters
 *   --magic-port-mode line   : buffer until CR/LF, output full line
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       magic_port_demo.c -o magic_port_demo -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* 16-bit port address for magic port — use OUT (C),reg via helper */
#define MAGIC_PORT  0xCAFE

static void magic_out(unsigned char val) __naked
{
    (void)val;
    __asm
        pop  hl         ; return address
        pop  de         ; val in E
        push de
        push hl
        ld   bc, #MAGIC_PORT
        out  (c), e
        ret
    __endasm;
}

/* ROM charset helper */
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

/* Send a string to the magic port, byte by byte, ending with newline */
static void magic_puts(const char *s)
{
    while (*s) {
        magic_out(*s++);
    }
    magic_out('\n');
}

void main(void)
{
    unsigned char i;

    /* Red border, clear screen */
    zx_border(2);

    /* Set attributes: white ink, red paper (0x11) */
    {
        unsigned char *attr = (unsigned char *)0x5800;
        unsigned int j;
        for (j = 0; j < 768; j++) attr[j] = 0x17;
    }

    /* Clear pixel area */
    {
        unsigned char *px = (unsigned char *)0x4000;
        unsigned int j;
        for (j = 0; j < 6144; j++) px[j] = 0;
    }

    print_str(6, 2, "MAGIC PORT DEMO");
    print_str(1, 4, "Writing to port 0xFF...");
    print_str(1, 5, "Check stderr for output.");

    /* Test 1: individual bytes */
    print_str(1, 7, "Test 1: Bytes A,B,C");
    magic_out('A');
    magic_out('B');
    magic_out('C');
    magic_out('\n');

    /* Test 2: string via magic_puts */
    print_str(1, 9, "Test 2: Hello string");
    magic_puts("Hello from ZX Next!");

    /* Test 3: counter */
    print_str(1, 11, "Test 3: Counter 0-9");
    for (i = 0; i < 10; i++) {
        magic_out('0' + i);
    }
    magic_out('\n');

    /* Test 4: debug checkpoint */
    print_str(1, 13, "Test 4: Checkpoint msg");
    magic_puts("Checkpoint 1 reached OK");

    print_str(1, 15, "All tests done!");

    for (;;) {
        intrinsic_halt();
    }
}
