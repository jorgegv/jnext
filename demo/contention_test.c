/*
 * Memory Contention Test
 *
 * Measures time to complete a tight loop reading from contended memory
 * (0x4000) vs uncontended memory (0x8000). On 48K/128K, reads from
 * 0x4000-0x7FFF are slowed by ULA contention during active display.
 * On Pentagon/Next, there is no contention.
 *
 * Method: run 4096 iterations of 10x LD A,(addr), then read the
 * FRAMES system variable (23672) to measure elapsed time in 1/50s.
 * Compare contended vs uncontended timing.
 *
 * Expected:
 *   48K/128K: contended takes MORE frames (yellow border)
 *   Pentagon/Next: both take SAME frames (cyan border)
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

/* Run 4096 iterations of 10x LD A,(HL). Returns elapsed FRAMES count. */
static unsigned int timed_reads(unsigned char *addr) __naked {
    (void)addr;
    __asm
        ; HL = address to read from
        ; Sync to frame boundary and read FRAMES counter
        ei
        halt

        ; Read FRAMES (23672 = 0x5C78, low byte) into C
        ld      a, (0x5C78)
        ld      c, a

        ; Run tight loop: 4096 iterations of 10x LD A,(HL)
        ld      b, #0           ; B = 0 means 256 iterations
        ld      d, #16          ; D = outer loop counter (16 * 256 = 4096)
    00101$:
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        ld      a, (hl)
        djnz    00101$
        dec     d
        jr      nz, 00101$

        ; Read FRAMES counter again
        ld      a, (0x5C78)

        ; Compute elapsed = new_frames - old_frames
        sub     c
        ld      l, a
        ld      h, #0
        ret
    __endasm;
}

int main(void) {
    unsigned int time_contended, time_uncontended;
    unsigned char *attrs;

    /* Clear screen */
    memset((void *)0x4000, 0, 6144);
    memset((void *)0x5800, 0x38, 768);

    /* Measure contended reads (0x4000 = screen memory) */
    time_contended = timed_reads((unsigned char *)0x4000);

    /* Measure uncontended reads (0x8000 = normal RAM) */
    time_uncontended = timed_reads((unsigned char *)0x8000);

    /* Display results as attribute patterns on lines 10 and 12 */
    /* Line 10: contended time (number of frames) */
    attrs = (unsigned char *)0x5800 + 10 * 32;
    attrs[0] = (unsigned char)(time_contended & 0xFF);
    attrs[1] = 0x47;  /* white marker */

    /* Line 12: uncontended time */
    attrs = (unsigned char *)0x5800 + 12 * 32;
    attrs[0] = (unsigned char)(time_uncontended & 0xFF);
    attrs[1] = 0x47;

    /* Border colour indicates result */
    if (time_contended > time_uncontended) {
        ula_port = 6;  /* yellow = contention detected */
    } else {
        ula_port = 5;  /* cyan = no contention */
    }

    /* Line 14: difference indicator with colour bar */
    attrs = (unsigned char *)0x5800 + 14 * 32;
    {
        int diff = (int)time_contended - (int)time_uncontended;
        unsigned char colour = (diff > 0) ? 0x46 : 0x45;  /* yellow or cyan */
        int bar_len = diff;
        if (bar_len < 0) bar_len = -bar_len;
        if (bar_len > 31) bar_len = 31;
        memset(attrs, colour, bar_len > 0 ? bar_len : 1);
    }

    for (;;) {
        intrinsic_halt();
    }

    return 0;
}
