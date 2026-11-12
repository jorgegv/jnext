/*
 * Save Counter
 *
 * Test program for the SD card's write journal: it keeps a run counter in one
 * raw sector of the card, read and written over the SPI ports (0xE7 chip
 * select, 0xEB data) with CMD17 and CMD24, the way a program with its own SD
 * driver does. Each run reads the sector (counter 0 when it does not start
 * with "JNSV"), adds 1, writes it back, prints "SAVE <n> OK" (or "SAVE ERR
 * <step>") on the magic port 0xCAFE, shows n & 7 as the border colour and halts.
 *
 * Why not esxDOS files: a NEX loaded directly (--load) has its RST $08 calls
 * answered by JNEXT's own stand-in, which never touches the card (see the man
 * page, "esxDOS calls from a directly loaded NEX"), so a file saved that way
 * would never reach it. SPI reaches the card in every start mode.
 *
 * The sector is SECTOR (block address of an SDHC card): well inside the 40 MiB
 * minimal NextZXOS card and far past its FAT and files, so nothing of
 * NextZXOS's is touched.
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
#include <z80.h>
#include <intrinsic.h>

#define MAGIC_PORT 0xCAFE
#define SPI_CS     0x00E7
#define SPI_DATA   0x00EB
#define SECTOR     60000UL

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

static void fail(unsigned char step)
{
    say("SAVE ERR ");
    say_num(step);
    say("\n");
    for (;;)
        intrinsic_halt();
}

/* Sending a byte is a write of the data port. Reading is a read of it, which returns the byte
   the previous read fetched and fetches the next (the SPI master is one byte behind), so a
   response is polled for rather than counted. */
static void put(unsigned char v) { z80_outp(SPI_DATA, v); }
static unsigned char get(void)  { return z80_inp(SPI_DATA); }

/* A command; returns R1 (0xFF when the card never answered). */
static unsigned char cmd(unsigned char c, unsigned long arg, unsigned char crc)
{
    unsigned char r, i;
    put(0xFF);
    put(0x40 | c);
    put((unsigned char)(arg >> 24));
    put((unsigned char)(arg >> 16));
    put((unsigned char)(arg >> 8));
    put((unsigned char)arg);
    put(crc);
    for (i = 0; i < 32; i++) {
        r = get();
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

void main(void)
{
    unsigned char n = 0, b[5], r;
    unsigned int i;

    z80_outp(SPI_CS, 0xFE);                 /* select the SD card */
    cmd(0, 0, 0x95);                        /* CMD0 */
    cmd(8, 0x1AA, 0x87);                    /* CMD8 */
    get(); get(); get(); get();             /* the rest of R7 */
    for (i = 0; i < 1000; i++) {            /* ACMD41 with HCS until ready */
        cmd(55, 0, 0xFF);
        if (cmd(41, 0x40000000UL, 0xFF) == 0) break;
    }
    if (i == 1000) fail(1);

    /* read the sector: counter n when it starts with "JNSV" */
    if (cmd(17, SECTOR, 0xFF) != 0) fail(2);
    for (i = 0; i < 2000 && get() != 0xFE; i++)
        ;
    if (i == 2000) fail(3);
    for (i = 0; i < 512; i++) {
        r = get();
        if (i < 5) b[i] = r;
    }
    get(); get();                           /* CRC */
    if (b[0] == 'J' && b[1] == 'N' && b[2] == 'S' && b[3] == 'V') n = b[4];

    n++;

    /* write it back */
    if (cmd(24, SECTOR, 0xFF) != 0) fail(4);
    put(0xFF);
    put(0xFE);                              /* start-of-block token */
    put('J'); put('N'); put('S'); put('V'); put(n);
    for (i = 5; i < 512; i++) put(0);
    put(0xFF); put(0xFF);                   /* CRC */
    for (i = 0; i < 64; i++) {
        r = get();
        if ((r & 0x1F) == 0x05) break;
    }
    if (i == 64) fail(5);
    for (i = 0; i < 20000 && get() != 0xFF; i++)   /* busy */
        ;
    z80_outp(SPI_CS, 0xFF);                 /* deselect */

    zx_border(n & 7);
    say("SAVE ");
    say_num(n);
    say(" OK\n");
    for (;;)
        intrinsic_halt();
}
