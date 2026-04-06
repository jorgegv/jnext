/*
 * Memory Contention Test
 *
 * Measures time to complete a tight loop reading from contended memory
 * (0x4000) vs uncontended memory (0x8000). On 48K/128K/+3, reads from
 * 0x4000-0x7FFF are slowed by ULA contention during active display.
 * On Pentagon/Next, there is no contention.
 *
 * Two versions: IM1 (uses ROM ISR + FRAMES sysvar, works on 48K)
 *               IM2 (custom ISR, works on all machines)
 *
 * Build:
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
 *       contention_test.c -o contention_test -subtype=nex -create-app
 */

#pragma output REGISTER_SP  = 0xfdfd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

__sfr __at 0xFE ula_port;

/* Frame counter incremented by our custom ISR */
volatile unsigned char frame_count;

/* Install a minimal IM2 ISR that increments frame_count.
 * IM2 vector table at 0xBE00, all entries 0xBD → ISR at 0xBDBD.
 */
static void install_isr(void) __naked {
    __asm
        di

        ; Build IM2 vector table at 0xBE00 (257 bytes of 0xBD)
        ld      hl, #0xBE00
        ld      de, #0xBE01
        ld      bc, #256
        ld      (hl), #0xBD
        ldir

        ; Write ISR at 0xBDBD:
        ; push af / push hl / ld hl,#_frame_count / inc (hl) / pop hl / pop af / ei / reti
        ld      hl, #0xBDBD
        ld      (hl), #0xF5         ; push af
        inc     hl
        ld      (hl), #0xE5         ; push hl
        inc     hl
        ld      (hl), #0x21         ; ld hl, imm16
        inc     hl
        ld      de, #_frame_count
        ld      (hl), e             ; low byte of frame_count address
        inc     hl
        ld      (hl), d             ; high byte of frame_count address
        inc     hl
        ld      (hl), #0x34         ; inc (hl)
        inc     hl
        ld      (hl), #0xE1         ; pop hl
        inc     hl
        ld      (hl), #0xF1         ; pop af
        inc     hl
        ld      (hl), #0xFB         ; ei
        inc     hl
        ld      (hl), #0xED         ; reti (0xED 0x4D)
        inc     hl
        ld      (hl), #0x4D

        ; Set I register and enable IM2
        ld      a, #0xBE
        ld      i, a
        im      2
        ei
        ret
    __endasm;
}

/* Run 40960 iterations of 10x LD A,(HL). Returns elapsed frame count. */
static unsigned int timed_reads(unsigned char *addr) __naked {
    (void)addr;
    __asm
        ; HL = address to read from
        ; Sync to frame boundary
        ei
        halt

        ; Reset frame counter
        xor     a
        ld      (_frame_count), a

        ; Run tight loop: 40960 iterations of 10x LD A,(HL)
        ld      b, #0           ; B = 0 means 256 iterations
        ld      d, #160         ; D = outer loop counter (160 * 256 = 40960)
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

        ; Read elapsed frames
        ld      a, (_frame_count)
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

    /* Install our own IM2 ISR so we don't depend on ROM */
    install_isr();

    /* Measure contended reads (0x4000 = screen memory) */
    time_contended = timed_reads((unsigned char *)0x4000);

    /* Measure uncontended reads (0x8000 = normal RAM) */
    time_uncontended = timed_reads((unsigned char *)0x8000);

    /* Disable interrupts so ISR doesn't override our border colour */
    intrinsic_di();

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
    }

    return 0;
}
