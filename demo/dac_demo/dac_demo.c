/*
 * ZX Spectrum Next -- DAC (Soundrive) stereo waveform demo
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 -subtype=nex dac_demo.c -o dac_demo -create-app
 *
 * Produces dac_demo.nex loadable in the emulator or real hardware.
 *
 * Outputs two sine waves at different frequencies, swapping between
 * left and right channels every ~0.5 seconds.  CPU runs at 28 MHz;
 * the measured loop period is ~878 T, i.e. a sample rate of ~32 kHz.
 *
 * DAC ports, per VHDL (authoritative):
 *
 *   zxnext.vhd:2429  soundrive mode 1 = ports 1F/0F/4F/5F for ch A/B/C/D
 *   zxnext.vhd:2661-2664  per-channel port decode equations
 *   soundrive.vhd:112-113 pcm_L = chA + chB,  pcm_R = chC + chD
 *
 * So the channel-to-side mapping is:
 *
 *   0x1F -> chA -> LEFT      0x4F -> chC -> RIGHT
 *   0x0F -> chB -> LEFT      0x5F -> chD -> RIGHT
 *
 * NOTE: port 0xDF is NOT channel D.  It is the Specdrum mono port and
 * writes channels A *and* D together (zxnext.vhd:2661,2664 via
 * port_dac_mono_AD), i.e. one sample to each side.  An earlier version of
 * this demo used 0xDF believing it addressed channel D on the left, and
 * also had B/D on the wrong sides.  The result was chA=chD=sample_a and
 * chB=chC=sample_b, so BOTH sides carried sample_a+sample_b: the stereo
 * separation never happened, and because sample_b is exactly the third
 * harmonic of sample_a (phase step 3 vs 1 over the same 256-entry table)
 * the sum was heard as one hollow, reedy, buzzing tone.  See issue #38.
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* ------------------------------------------------------------------ *
 * NextREG constants                                                   *
 * ------------------------------------------------------------------ */
#define NR_CPU_SPEED     0x07   /* bits 1:0 = CPU speed (0=3.5,1=7,2=14,3=28 MHz) */
#define NR_PERIPHERAL_3  0x08   /* bit 3 = enable Soundrive/Covox DAC */

/* ------------------------------------------------------------------ *
 * ULA screen helpers                                                  *
 * ------------------------------------------------------------------ */

/* ZX Spectrum character set base address.
 * The ROM font starts at 0x3D00 for character 0x20 (space).
 * Using 0x3C00 (= 0x3D00 - 0x20*8) so that (base + ch*8) gives
 * the correct glyph for any printable ASCII character. */
#define ROM_CHARSET 0x3C00

static void print_char(unsigned char col, unsigned char row, unsigned char ch)
{
    unsigned char *screen;
    const unsigned char *font;
    unsigned char i;

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

static void cls(void)
{
    unsigned char *p;
    unsigned int i;

    p = (unsigned char *)0x4000;
    for (i = 0; i < 6144; i++)
        p[i] = 0;

    p = (unsigned char *)0x5800;
    for (i = 0; i < 768; i++)
        p[i] = 0x07;
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
 * Pure-assembly playback loop.                                        *
 *                                                                     *
 * Register allocation:                                                *
 *   C  = phase_a (step +1, low tone)                                  *
 *   E  = phase_b (step +3, high tone)                                 *
 *   D  = swap flag (0 or 1)                                           *
 *   IY = pointer to sine_table                                        *
 *   IX = swap counter (counts down from SWAP_COUNT)                   *
 *                                                                     *
 * Timing per sample iteration (fixed path):                           *
 *   ~358 T core + 520 T delay = ~878 T → 28M/878 ≈ 31.9 kHz         *
 *   Step 1 → 31900/256 ≈ 125 Hz                                      *
 *   Step 3 → 31900/256*3 ≈ 374 Hz                                    *
 * (measured with --dac-trace, not estimated)                          *
 * ------------------------------------------------------------------ */
static void playback(void) __naked
{
    (void)sine_table;  /* ensure sine_table is emitted */
    __asm

    ;; Load sine_table address into IY
    ld   iy, _sine_table

    ;; Init phases and swap
    ld   c, 0           ; phase_a = 0
    ld   e, 0           ; phase_b = 0
    ld   d, 0           ; swap = 0
    ld   ix, 14000      ; swap counter (14000 samples ≈ 0.44s at ~32 kHz)

_play_loop:
    ;; Look up sample_a = sine_table[phase_a] → B
    ld   b, 0
    push iy
    pop  hl             ; HL = sine_table base
    add  hl, bc         ; HL = &sine_table[phase_a]
    ld   b, (hl)        ; B = sample_a

    ;; Look up sample_b = sine_table[phase_b] → L (reuse HL)
    push iy
    pop  hl             ; HL = sine_table base
    ld   a, 0
    add  a, e           ; A = phase_b (just load E really)
    ;; need to add E to HL
    push de
    ld   d, 0
    add  hl, de         ; HL = &sine_table[phase_b]
    pop  de
    ld   a, (hl)        ; A = sample_b, B = sample_a

    ;; Select which sample goes to which side: L in L, R in H.
    ;; Both arms are deliberately the same length (35 T) so the sample rate
    ;; -- and therefore the pitch -- does not change when the channels swap.
    bit  0, d           ; 8
    jr   nz, _dac_swapped   ; 7 not taken / 12 taken

    ;; swap=0: left = sample_a (B), right = sample_b (A)
    ld   l, b           ; 4
    ld   h, a           ; 4
    jr   _dac_done_out  ; 12   -> arm total 35 T

_dac_swapped:
    ;; swap=1: left = sample_b (A), right = sample_a (B)
    ld   l, a           ; 4
    ld   h, b           ; 4
    ld   b, 0           ; 7  timing pad only (B is dead here; DJNZ reloads it)
                        ;    -> arm total 35 T, equal to the arm above

_dac_done_out:
    ;; Emit: chA+chB are LEFT, chC+chD are RIGHT (soundrive.vhd:112-113).
    ;; Both channels of a side get the same value, so each side reproduces
    ;; its tone at full amplitude.
    ld   a, l
    out  (0x1f), a      ; chA -> LEFT
    out  (0x0f), a      ; chB -> LEFT
    ld   a, h
    out  (0x4f), a      ; chC -> RIGHT
    out  (0x5f), a      ; chD -> RIGHT
    ;; Advance phases
    inc  c              ; phase_a += 1
    ld   a, e
    add  a, 3
    ld   e, a           ; phase_b += 3

    ;; Decrement swap counter
    push hl
    push ix
    pop  hl
    dec  hl
    ld   a, h
    or   l
    push hl
    pop  ix
    pop  hl
    jr   nz, _dac_no_swap

    ;; Counter reached 0: swap channels, reset counter
    ld   a, d
    xor  1
    ld   d, a
    ld   ix, 14000

_dac_no_swap:
    ;; Delay for sample rate control (40 × DJNZ at 13 T = 520 T)
    ld   b, 40
_dac_delay:
    djnz _dac_delay

    jr   _play_loop

    __endasm;
}

/* ------------------------------------------------------------------ *
 * main                                                                *
 * ------------------------------------------------------------------ */
int main(void)
{
    /* 1. Clear screen and print title. */
    cls();
    print_str(11, 2, "DAC DEMO");
    set_attr(11, 2, 0x45, 8);   /* BRIGHT 1, INK 5 (cyan), PAPER 0 */

    print_str(4, 5, "Soundrive 4-channel DAC");
    print_str(4, 6, "CPU at 28 MHz, ~32 kHz rate");
    print_str(4, 8, "Low tone and high tone swap");
    print_str(4, 9, "between L and R every 0.5s.");

    print_str(6, 16, "Press BREAK to stop.");

    /* 2. Switch to 28 MHz for adequate sample rate. */
    ZXN_WRITE_REG(NR_CPU_SPEED, 0x03);

    /* 3. Enable Soundrive DAC mode via NextREG 0x08 bit 3. */
    ZXN_WRITE_REG(NR_PERIPHERAL_3, 0x08);

    /* 4. Disable interrupts and enter pure-asm playback loop. */
    intrinsic_di();
    playback();

    return 0;
}
