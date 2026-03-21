/*
 * ZX Spectrum Next -- DAC (Soundrive) stereo waveform demo
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 -subtype=nex dac_demo.c -o dac_demo -create-app
 *
 * Produces dac_demo.nex loadable in the emulator or real hardware.
 *
 * Outputs a sine wave on DAC channels A+D (left speaker) and a
 * different-frequency sine wave on channels B+C (right speaker),
 * producing a stereo effect.
 *
 * DAC ports verified against VHDL source:
 *   cores/zxnext/src/audio/soundrive.vhd
 *   cores/zxnext/src/zxnext.vhd  (port decoding + NextREG 0x08)
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* ------------------------------------------------------------------ *
 * Soundrive DAC ports (Mode 1 -- active by default on ZX Next)       *
 *   Channel A (left):   port 0x1F                                    *
 *   Channel B (right):  port 0x0F                                    *
 *   Channel C (right):  port 0x4F                                    *
 *   Channel D (left):   port 0xDF                                    *
 * 8-bit decoded -- __sfr is correct.                                 *
 * ------------------------------------------------------------------ */
__sfr __at 0x1F IO_DAC_A;
__sfr __at 0x0F IO_DAC_B;
__sfr __at 0x4F IO_DAC_C;
__sfr __at 0xDF IO_DAC_D;

/* ------------------------------------------------------------------ *
 * NextREG constants                                                   *
 * ------------------------------------------------------------------ */
#define NR_PERIPHERAL_3  0x08   /* bit 2 = enable Soundrive/Covox DAC */

/* ------------------------------------------------------------------ *
 * ULA screen helpers -- print text at attribute position              *
 * ------------------------------------------------------------------ */

/* ZX Spectrum character set address in ROM (0x3D00). */
#define ROM_CHARSET 0x3D00

static void print_char(unsigned char col, unsigned char row, unsigned char ch)
{
    unsigned char *screen;
    const unsigned char *font;
    unsigned char i;

    /* Calculate screen address for character cell (col, row).
     * Screen layout: base 0x4000, each character row = 8 pixel rows.
     * Pixel row address = 0x4000 + ((row & 0x18) << 8) + ((row & 0x07) << 5) + col
     * But characters span 8 pixel lines, so we iterate over the
     * 3-bit line counter (bits 8-10 of address). */
    font = (const unsigned char *)(ROM_CHARSET + ((unsigned int)ch << 3));

    for (i = 0; i < 8; i++) {
        screen = (unsigned char *)(0x4000u
                    + ((unsigned int)(row & 0x18) << 8)
                    + ((unsigned int)(i & 0x07) << 8)
                    + ((unsigned int)(row & 0x07) << 5)
                    + col);
        *screen = font[i];
    }
}

static void print_str(unsigned char col, unsigned char row, const char *s)
{
    while (*s) {
        print_char(col++, row, (unsigned char)*s++);
    }
}

static void set_attr(unsigned char col, unsigned char row,
                     unsigned char attr, unsigned char len)
{
    unsigned char *p = (unsigned char *)(0x5800u + (unsigned int)row * 32 + col);
    unsigned char i;
    for (i = 0; i < len; i++)
        p[i] = attr;
}

/* Clear the ULA screen. */
static void cls(void)
{
    unsigned char *p;
    unsigned int i;

    /* Clear pixel area. */
    p = (unsigned char *)0x4000;
    for (i = 0; i < 6144; i++)
        p[i] = 0;

    /* Set all attributes to white ink on black paper. */
    p = (unsigned char *)0x5800;
    for (i = 0; i < 768; i++)
        p[i] = 0x07;  /* INK 7 (white), PAPER 0 (black) */
}

/* ------------------------------------------------------------------ *
 * Pre-computed 256-entry sine table (unsigned 8-bit: 0..255)          *
 * sin(i * 2*PI / 256) * 127 + 128, clamped to [0, 255]              *
 * ------------------------------------------------------------------ */
static const unsigned char sine_table[256] = {
    128, 131, 134, 137, 140, 143, 146, 149,
    152, 155, 158, 162, 165, 167, 170, 173,
    176, 179, 182, 185, 188, 190, 193, 196,
    198, 201, 203, 206, 208, 211, 213, 215,
    218, 220, 222, 224, 226, 228, 230, 232,
    234, 235, 237, 239, 240, 241, 243, 244,
    245, 246, 248, 249, 250, 250, 251, 252,
    253, 253, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 254, 253,
    253, 252, 251, 250, 250, 249, 248, 246,
    245, 244, 243, 241, 240, 239, 237, 235,
    234, 232, 230, 228, 226, 224, 222, 220,
    218, 215, 213, 211, 208, 206, 203, 201,
    198, 196, 193, 190, 188, 185, 182, 179,
    176, 173, 170, 167, 165, 162, 158, 155,
    152, 149, 146, 143, 140, 137, 134, 131,
    128, 124, 121, 118, 115, 112, 109, 106,
    103, 100,  97,  93,  90,  88,  85,  82,
     79,  76,  73,  70,  67,  65,  62,  59,
     57,  54,  52,  49,  47,  44,  42,  40,
     37,  35,  33,  31,  29,  27,  25,  23,
     21,  20,  18,  16,  15,  14,  12,  11,
     10,   9,   7,   6,   5,   5,   4,   3,
      2,   2,   1,   1,   1,   0,   0,   0,
      0,   0,   0,   0,   1,   1,   1,   2,
      2,   3,   4,   5,   5,   6,   7,   9,
     10,  11,  12,  14,  15,  16,  18,  20,
     21,  23,  25,  27,  29,  31,  33,  35,
     37,  40,  42,  44,  47,  49,  52,  54,
     57,  59,  62,  65,  67,  70,  73,  76,
     79,  82,  85,  88,  90,  93,  97, 100,
    103, 106, 109, 112, 115, 118, 121, 124
};

/* ------------------------------------------------------------------ *
 * Small busy-wait delay for approximate sample rate control.          *
 * Each iteration is roughly 4 T-states (DEC B; JR NZ).               *
 * At 3.5 MHz, 40 iterations ~ 46 us ~ 21.7 kHz sample rate.         *
 * ------------------------------------------------------------------ */
static void delay(unsigned char n) __naked
{
    (void)n;
    __asm
        ;; n is in L on entry (sdcc_iy calling convention)
        ld   b, l
    _delay_loop:
        djnz _delay_loop
        ret
    __endasm;
}

/* ------------------------------------------------------------------ *
 * main                                                                *
 * ------------------------------------------------------------------ */
int main(void)
{
    unsigned char phase_a = 0;
    unsigned char phase_b = 0;
    unsigned char sample_a, sample_b;

    /* 1. Clear screen and print title. */
    cls();
    print_str(11, 2, "DAC DEMO");
    set_attr(11, 2, 0x45, 8);   /* BRIGHT 1, INK 5 (cyan), PAPER 0 */

    print_str(4, 5, "Soundrive 4-channel DAC");
    print_str(4, 7, "Left:  sine wave (ch A+D)");
    print_str(4, 9, "Right: sine wave (ch B+C)");
    print_str(4, 11, "Different frequencies for");
    print_str(4, 12, "stereo effect.");

    print_str(6, 16, "Press BREAK to stop.");

    /* 2. Enable Soundrive DAC mode via NextREG 0x08 bit 2. */
    ZXN_WRITE_REG(NR_PERIPHERAL_3, 0x04);

    /* 3. Main loop: output samples continuously.
     *    phase_a increments by 1 each sample (lower frequency).
     *    phase_b increments by 3 each sample (higher frequency).
     *    This produces two distinct tones, one per stereo channel. */
    while (1) {
        sample_a = sine_table[phase_a];
        sample_b = sine_table[phase_b];

        IO_DAC_A = sample_a;    /* left  */
        IO_DAC_D = sample_a;    /* left  */
        IO_DAC_B = sample_b;    /* right */
        IO_DAC_C = sample_b;    /* right */

        phase_a += 1;   /* step 1: lower frequency */
        phase_b += 3;   /* step 3: higher frequency */

        /* Small delay for ~22 kHz sample rate.
         * 40 iterations of DJNZ at 3.5 MHz ~ 46 us per sample. */
        delay(40);
    }

    return 0;
}
