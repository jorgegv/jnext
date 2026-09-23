/*
 * Joystick-port UART echo demo (GH #252)
 *
 * The guest half of the live bidirectional joy-port serial cable. It does the
 * one thing a bench needs and nothing else:
 *
 *   1. routes the joystick connector's serial lines to UART 0 by writing
 *      NR 0x0B = 0xB0 — enable (bit 7) + UART pin-7 mode (bit 5) + connector
 *      joy 2 (bit 4) + channel UART 0 (bit 0), per zxnext.vhd:3536-3538 and
 *      :3340-3341;
 *   2. transmits one 'R' so a host bench knows the mux is live;
 *   3. forever: when a byte is available on UART 0, read it and transmit
 *      BYTE + 1 back.
 *
 * WHY +1 AND NOT AN ECHO. A plain echo is indistinguishable from the emulator
 * looping the transmitted byte back into its own RX FIFO, which is the exact
 * defect UartChannel::deliver_tx_byte's isolation branch exists to prevent. A
 * host that sends "ABC" and reads back "BCD" has proved that the bytes went
 * through the Z80 and came out of the transmit path; "ABC" would not.
 *
 * Run it:
 *   mkfifo /tmp/cable.rx /tmp/cable.tx        # or let jnext create them
 *   jnext --headless --joy-uart-fifo /tmp/cable --load joy_uart_demo.nex
 *   # in another shell:
 *   exec 3>/tmp/cable.rx; printf 'ABC' >&3
 *   head -c 4 /tmp/cable.tx                   # -> RBCD
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 \
 *       joy_uart_demo.c -o joy_uart_demo.bin -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* UART channel-select port. zxn.h declares 0x133B / 0x143B but not this one. */
__sfr __banked __at 0x153b IO_UART_SELECT;

/* NR 0x0B, the joystick I/O-mode multiplexer (zxnext.vhd:5200-5203).
 *   bit 7    enable                        (nr_0b_joy_iomode_en)
 *   bits 5:4 pin-7 mode / RX connector      (nr_0b_joy_iomode)
 *            "1x" = UART; bit 4: 0 = joy 1, 1 = joy 2  (zxnext.vhd:3538)
 *   bit 0    which UART channel             (zxnext.vhd:3340-3341)
 *
 * 0xB0 = enabled, UART mode, connector joy 2, channel UART 0 — which is what
 * jnext's own default (--joy-uart-connector 2) plugs the cable into.
 */
#define NR_JOY_IOMODE       0x0B
#define JOY_UART_JOY2_UART0 0xB0

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

static void print_hex(unsigned char col, unsigned char row, unsigned char v)
{
    static const char digits[] = "0123456789ABCDEF";
    print_char(col,     row, digits[v >> 4]);
    print_char(col + 1, row, digits[v & 0x0F]);
}

void main(void)
{
    unsigned char count = 0;
    unsigned char col   = 0;
    unsigned char row   = 8;

    zx_border(1);

    /* White ink on blue paper, cleared pixels. */
    {
        unsigned char *attr = (unsigned char *)0x5800;
        unsigned char *px   = (unsigned char *)0x4000;
        unsigned int j;
        for (j = 0; j < 768;  j++) attr[j] = 0x0F;
        for (j = 0; j < 6144; j++) px[j]   = 0;
    }

    print_str(2, 2, "JOY-PORT UART ECHO");
    print_str(2, 6, "RX:");

    /* Select UART 0 (bit 6 = 0); bit 4 clear, so bits 2:0 are not taken as a
     * prescaler MSB write. The 115200 8N1 reset default is left alone. */
    IO_UART_SELECT = 0x00;

    /* Route the joystick connector to the UART. */
    ZXN_NEXTREG(NR_JOY_IOMODE, JOY_UART_JOY2_UART0);

    /* One 'R' the instant the mux is live. A host bench has no other way to
     * know the guest is listening: bytes sent before this point are LOST on
     * the wire, exactly as they are on real hardware, so a bench that just
     * slept for a while would be a race dressed as a delay. The byte is
     * transmitted whether or not anything is reading yet — the emulator queues
     * it and hands it over when a reader attaches. */
    while (IO_UART_STATUS & 0x02) { }
    IO_UART_TX = 'R';
    print_str(2, 4, "NR 0B=B0  UART0  JOY 2  R");

    for (;;) {
        if (IO_UART_STATUS & IUS_RX_AVAIL) {
            unsigned char b = IO_UART_RX;

            /* Wait for room in the 64-byte TX FIFO (status bit 1 = TX full). */
            while (IO_UART_STATUS & 0x02) { }
            IO_UART_TX = (unsigned char)(b + 1);

            /* Show what arrived, wrapping across a few lines. */
            print_hex(col, row, b);
            col += 3;
            if (col >= 30) { col = 0; if (++row >= 20) row = 8; }

            print_str(6, 6, "        ");
            print_hex(6, 6, ++count);
        }
    }
}
