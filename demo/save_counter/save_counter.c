/*
 * Save Counter
 *
 * Test program for the SD card's write journal: it keeps a one-byte run
 * counter in /JNSAVE.DAT on the card. Each run reads the counter (0 when the
 * file is absent), adds 1, rewrites the file, prints "SAVE <n> OK" (or
 * "SAVE ERR <esxdos error>") on the magic port 0xCAFE, shows n & 7 as the
 * border colour and halts.
 *
 * Needs esxDOS (NextZXOS), so only a NEX is built: a 48K TAP has none.
 *
 *   ./build/jnext --magic-port 0xCAFE --magic-port-mode line \
 *       --load test/fixtures/nex/save_counter.nex
 *
 * Build (the Makefile in this directory does the same):
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 \
 *       save_counter.c -o save_counter.bin -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <arch/zxn/esxdos.h>
#include <z80.h>
#include <errno.h>
#include <intrinsic.h>

#define MAGIC_PORT 0xCAFE

static void say(const char *s)
{
    while (*s)
        z80_outp(MAGIC_PORT, (unsigned char)*s++);
}

static void say_num(unsigned char n)
{
    char buf[4];
    unsigned char i = 0;
    if (n >= 100) buf[i++] = '0' + n / 100;
    if (n >= 10)  buf[i++] = '0' + (n / 10) % 10;
    buf[i++] = '0' + n % 10;
    buf[i] = 0;
    say(buf);
}

static void fail(unsigned char e)
{
    say("SAVE ERR ");
    say_num(e);
    say("\n");
    for (;;)
        intrinsic_halt();
}

void main(void)
{
    static const char name[] = "/JNSAVE.DAT";
    unsigned char n = 0;
    unsigned char h;

    errno = 0;
    h = esx_f_open(name, ESXDOS_MODE_R | ESXDOS_MODE_OPEN_EXIST);
    if (h != 0xff && errno == 0) {
        esx_f_read(h, &n, 1);
        esx_f_close(h);
    }

    n++;
    errno = 0;
    h = esx_f_open(name, ESXDOS_MODE_W | ESXDOS_MODE_CREAT_TRUNC);
    if (errno) fail((unsigned char)errno);
    esx_f_write(h, &n, 1);
    if (errno) fail((unsigned char)errno);
    esx_f_close(h);
    if (errno) fail((unsigned char)errno);

    zx_border(n & 7);
    say("SAVE ");
    say_num(n);
    say(" OK\n");
    for (;;)
        intrinsic_halt();
}
