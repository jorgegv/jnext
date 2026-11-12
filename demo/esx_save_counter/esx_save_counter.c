/*
 * ESX Save Counter
 *
 * Test program for the file the esxDOS stand-in of a directly loaded NEX keeps
 * (jnext answers RST $08 itself for such a NEX). It keeps a one-byte run
 * counter in /JNHI.DAT. Each run reads the counter (0 when the file is absent),
 * adds 1, rewrites the file, prints "ESX <n> OK" (or "ESX ERR <esxdos error>",
 * the error of the open, the write or the close) on the magic port 0xCAFE,
 * shows n & 7 as the border colour and halts.
 *
 * Built twice from this one source: VARIANT=1 and VARIANT=2 differ only in a
 * byte of initialised data, so the two NEX files are different programs with
 * the same file name and the same output.
 *
 * Needs esxDOS, so only a NEX is built: a 48K TAP has none.
 *
 *   ./build/jnext --magic-port 0xCAFE --magic-port-mode line \
 *       --load test/fixtures/nex/esx_save_counter.nex
 *
 * Build (the Makefile in this directory does the same):
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 -DVARIANT=1 \
 *       esx_save_counter.c -o esx_save_counter.bin -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <arch/zxn/esxdos.h>
#include <z80.h>
#include <errno.h>
#include <intrinsic.h>

#define MAGIC_PORT 0xCAFE

#ifndef VARIANT
#define VARIANT 1
#endif

/* Non-static initialised data: kept in the binary, so the variants differ. */
unsigned char variant_id = VARIANT;

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
    say("ESX ERR ");
    say_num(e);
    say("\n");
    for (;;)
        intrinsic_halt();
}

void main(void)
{
    static const char name[] = "/JNHI.DAT";
    unsigned char n = 0;
    unsigned char h;

    errno = 0;
    h = esx_f_open(name, ESXDOS_MODE_R | ESXDOS_MODE_OPEN_EXIST);
    if (h != 0xff && errno == 0) {
        esx_f_read(h, &n, 1);
        esx_f_close(h);
    }
    if (variant_id == 0) n = 0;   /* never true: references the variant byte */

    n++;
    errno = 0;
    h = esx_f_open(name, ESXDOS_MODE_W | ESXDOS_MODE_CREAT_TRUNC);
    if (errno) fail((unsigned char)errno);
    esx_f_write(h, &n, 1);
    if (errno) fail((unsigned char)errno);
    esx_f_close(h);
    if (errno) fail((unsigned char)errno);

    zx_border(n & 7);
    say("ESX ");
    say_num(n);
    say(" OK\n");
    for (;;)
        intrinsic_halt();
}
