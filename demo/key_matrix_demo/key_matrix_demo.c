/*
 * Key Matrix Demo
 *
 * Reports the keyboard state on the magic port, one line per change:
 *
 *   KMX r0 r1 r2 r3 r4 r5 r6 r7 b0 b1
 *
 * r0..r7 are the eight half-rows read from ports 0xFEFE, 0xFDFE, 0xFBFE, 0xF7FE,
 * 0xEFFE, 0xDFFE, 0xBFFE and 0x7FFE (low 5 bits, 1 = released), b0 and b1 are
 * NextReg 0xB0 and 0xB1 (the extended keys, 1 = pressed). All values are two-digit
 * upper-case hex. "KMX READY" is written once at the start. The same line is
 * drawn on the screen.
 *
 * Used by the browser test of the on-screen keyboard. Run with:
 *
 *   ./build/jnext --machine 48k --load key_matrix_demo.tap \
 *       --magic-port 0xCAFE --magic-port-mode line
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 \
 *       key_matrix_demo.c -o key_matrix_demo -subtype=tap -create-app
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

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

/* IN A,(port): the 16-bit port is in HL (fastcall), the byte comes back in L */
static unsigned char in_port(unsigned int port) __naked __z88dk_fastcall
{
    (void)port;
    __asm
        ld   b, h
        ld   c, l
        in   l, (c)
        ld   h, #0
        ret
    __endasm;
}

/* NextReg read: the register number is in L (fastcall), the value comes back in L */
static unsigned char nr_read(unsigned char reg) __naked __z88dk_fastcall
{
    (void)reg;
    __asm
        ld   bc, #0x243B
        out  (c), l
        ld   bc, #0x253B
        in   l, (c)
        ld   h, #0
        ret
    __endasm;
}

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

static void magic_puts(const char *s)
{
    while (*s) magic_out(*s++);
    magic_out('\n');
}

static const unsigned int rows[8] = {
    0xFEFE, 0xFDFE, 0xFBFE, 0xF7FE, 0xEFFE, 0xDFFE, 0xBFFE, 0x7FFE
};

/* "KMX " + 10 x "XX " ; the last space becomes the terminator */
static char line[4 + 10 * 3];

static void hex2(char *p, unsigned char v)
{
    static const char digits[] = "0123456789ABCDEF";
    p[0] = digits[v >> 4];
    p[1] = digits[v & 15];
}

void main(void)
{
    unsigned char now[10], last[10];
    unsigned char i, changed;

    intrinsic_di();
    magic_puts("KMX READY");
    for (i = 0; i < 10; i++) last[i] = 0xAA;   /* impossible state: the first scan prints a line */

    for (;;) {
        for (i = 0; i < 8; i++) now[i] = in_port(rows[i]) & 0x1F;
        now[8] = nr_read(0xB0);
        now[9] = nr_read(0xB1);
        changed = 0;
        for (i = 0; i < 10; i++) if (now[i] != last[i]) changed = 1;
        if (!changed) continue;

        line[0] = 'K'; line[1] = 'M'; line[2] = 'X'; line[3] = ' ';
        for (i = 0; i < 10; i++) {
            hex2(&line[4 + i * 3], now[i]);
            line[4 + i * 3 + 2] = ' ';
            last[i] = now[i];
        }
        line[4 + 10 * 3 - 1] = 0;
        magic_puts(line);
        print_str(0, 0, line);
    }
}
