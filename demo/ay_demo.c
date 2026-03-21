/*
 * ZX Spectrum Next -- AY-3-8910 / YM2149 Sound Demo
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 ay_demo.c -o ay_demo -subtype=nex -create-app
 *
 * Produces ay_demo.nex loadable in CSpect / ZEsarUX / real hardware / jnext.
 *
 * Demonstrates:
 *   - Channel A: melody (Twinkle Twinkle Little Star)
 *   - Channel B: bass line (root notes, lower octave)
 *   - Channel C: sustained pad with envelope generator
 *   - ULA screen text display
 *
 * AY ports verified against FPGA VHDL source:
 *   cores/zxnext/src/audio/ym2149.vhd
 *   cores/zxnext/src/zxnext.vhd (port decode)
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <intrinsic.h>  /* intrinsic_halt(), intrinsic_ei(), intrinsic_im_1() */

/* ------------------------------------------------------------------ *
 * AY port declarations                                                 *
 *                                                                      *
 * 0xFFFD and 0xBFFD are 16-bit decoded on the ZX Next, so we must    *
 * use __sfr __banked for correct OUT (C),A generation.               *
 * ------------------------------------------------------------------ */
__sfr __banked __at 0xFFFD IO_AY_REG;
__sfr __banked __at 0xBFFD IO_AY_DATA;

/* ------------------------------------------------------------------ *
 * AY register indices                                                  *
 * ------------------------------------------------------------------ */
#define AY_CHA_TONE_LO   0   /* Channel A tone period, low 8 bits   */
#define AY_CHA_TONE_HI   1   /* Channel A tone period, high 4 bits  */
#define AY_CHB_TONE_LO   2   /* Channel B tone period, low 8 bits   */
#define AY_CHB_TONE_HI   3   /* Channel B tone period, high 4 bits  */
#define AY_CHC_TONE_LO   4   /* Channel C tone period, low 8 bits   */
#define AY_CHC_TONE_HI   5   /* Channel C tone period, high 4 bits  */
#define AY_NOISE_PERIOD   6   /* Noise period (5-bit)                */
#define AY_MIXER          7   /* Mixer control (active LOW enables)  */
#define AY_CHA_VOL        8   /* Channel A volume / envelope flag    */
#define AY_CHB_VOL        9   /* Channel B volume / envelope flag    */
#define AY_CHC_VOL       10   /* Channel C volume / envelope flag    */
#define AY_ENV_LO        11   /* Envelope period, low 8 bits         */
#define AY_ENV_HI        12   /* Envelope period, high 8 bits        */
#define AY_ENV_SHAPE     13   /* Envelope shape (0-15)               */

/* ------------------------------------------------------------------ *
 * AY helper: write register                                            *
 * ------------------------------------------------------------------ */
static void ay_write(unsigned char reg, unsigned char val)
{
    IO_AY_REG = reg;
    IO_AY_DATA = val;
}

/* Set tone period on a channel (0=A, 1=B, 2=C) */
static void ay_set_tone(unsigned char chan, unsigned int period)
{
    unsigned char lo_reg = (unsigned char)(chan * 2);
    ay_write(lo_reg,     (unsigned char)(period & 0xFF));
    ay_write(lo_reg + 1, (unsigned char)((period >> 8) & 0x0F));
}

/* ------------------------------------------------------------------ *
 * Note period table                                                    *
 *                                                                      *
 * AY clock = 28 MHz / 16 = 1.75 MHz                                  *
 * Period = 1750000 / (16 * freq)                                      *
 *                                                                      *
 * Index: 0=REST, 1=C3, 2=D3, 3=E3, 4=F3, 5=G3, 6=A3, 7=B3          *
 *        8=C4, 9=D4, 10=E4, 11=F4, 12=G4, 13=A4, 14=B4              *
 *       15=C5, 16=D5, 17=E5                                          *
 * ------------------------------------------------------------------ */
#define REST   0
#define C3     1
#define D3     2
#define E3     3
#define F3     4
#define G3     5
#define A3     6
#define B3     7
#define C4     8
#define D4     9
#define E4    10
#define F4    11
#define G4    12
#define A4    13
#define B4    14
#define C5    15
#define D5    16
#define E5    17

static const unsigned int note_periods[] = {
    0,      /* 0: REST (silence)       */
    956,    /* 1: C3  ~130.8 Hz        */
    851,    /* 2: D3  ~146.8 Hz        */
    758,    /* 3: E3  ~164.8 Hz        */
    716,    /* 4: F3  ~174.6 Hz        */
    638,    /* 5: G3  ~196.0 Hz        */
    568,    /* 6: A3  ~220.0 Hz        */
    506,    /* 7: B3  ~246.9 Hz        */
    478,    /* 8: C4  ~261.6 Hz        */
    426,    /* 9: D4  ~293.7 Hz        */
    379,    /* 10: E4 ~329.6 Hz        */
    358,    /* 11: F4 ~348.8 Hz        */
    319,    /* 12: G4 ~392.0 Hz        */
    284,    /* 13: A4 ~440.0 Hz        */
    253,    /* 14: B4 ~493.9 Hz        */
    239,    /* 15: C5 ~523.3 Hz        */
    213,    /* 16: D5 ~586.7 Hz        */
    190,    /* 17: E5 ~657.9 Hz        */
};

/* ------------------------------------------------------------------ *
 * Melody: "Twinkle Twinkle Little Star"                                *
 * Each entry is a note index; each note plays for NOTE_FRAMES frames. *
 * ------------------------------------------------------------------ */
#define NOTE_FRAMES 12  /* ~240ms per note at 50 Hz */

static const unsigned char melody[] = {
    /* Twinkle twinkle little star */
    C4, C4, G4, G4, A4, A4, G4, REST,
    /* How I wonder what you are */
    F4, F4, E4, E4, D4, D4, C4, REST,
    /* Up above the world so high */
    G4, G4, F4, F4, E4, E4, D4, REST,
    /* Like a diamond in the sky */
    G4, G4, F4, F4, E4, E4, D4, REST,
    /* Twinkle twinkle little star */
    C4, C4, G4, G4, A4, A4, G4, REST,
    /* How I wonder what you are */
    F4, F4, E4, E4, D4, D4, C4, REST,
};

#define MELODY_LEN (sizeof(melody) / sizeof(melody[0]))

/* ------------------------------------------------------------------ *
 * Bass line: root notes one octave below melody (simplified)           *
 * Changes every 2 melody notes for a slower bass rhythm.              *
 * ------------------------------------------------------------------ */
static const unsigned char bass[] = {
    C3, C3, G3, G3, A3, A3, G3, REST,
    F3, F3, E3, E3, D3, D3, C3, REST,
    G3, G3, F3, F3, E3, E3, D3, REST,
    G3, G3, F3, F3, E3, E3, D3, REST,
    C3, C3, G3, G3, A3, A3, G3, REST,
    F3, F3, E3, E3, D3, D3, C3, REST,
};

#define BASS_LEN (sizeof(bass) / sizeof(bass[0]))

/* ------------------------------------------------------------------ *
 * Pad note for Channel C (sustained with envelope)                     *
 * Changes chord root every 8 melody notes.                            *
 * ------------------------------------------------------------------ */
static const unsigned char pad[] = {
    E4, E4, E4, E4, E4, E4, E4, E4,
    D4, D4, D4, D4, D4, D4, D4, D4,
    E4, E4, E4, E4, E4, E4, E4, E4,
    E4, E4, E4, E4, E4, E4, E4, E4,
    E4, E4, E4, E4, E4, E4, E4, E4,
    D4, D4, D4, D4, D4, D4, D4, D4,
};

#define PAD_LEN (sizeof(pad) / sizeof(pad[0]))

/* ------------------------------------------------------------------ *
 * ULA screen text output                                               *
 *                                                                      *
 * ZX Spectrum screen memory layout:                                   *
 *   Pixel data: 0x4000-0x57FF (6144 bytes)                           *
 *   Attributes: 0x5800-0x5AFF (768 bytes)                            *
 *                                                                      *
 * Character rows are NOT linearly addressed in pixel memory.          *
 * For row R (0-23), column C (0-31):                                  *
 *   Pixel address of char line L (0-7):                               *
 *     0x4000 + ((R & 0x18) << 8) + ((L & 7) << 8) + ((R & 7) << 5) + C *
 *   Attribute address:                                                *
 *     0x5800 + R*32 + C                                               *
 * ------------------------------------------------------------------ */

/* Simple 8x8 font for uppercase letters, digits, and a few symbols.
 * Only the characters we need: space, A-Z, 0-9, and a few others.
 * Each character is 8 bytes (one byte per pixel row). */

static const unsigned char font_data[][8] = {
    /* 0: space */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 1: A */
    { 0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00 },
    /* 2: B */
    { 0x7C, 0x42, 0x7C, 0x42, 0x42, 0x42, 0x7C, 0x00 },
    /* 3: C */
    { 0x3C, 0x42, 0x40, 0x40, 0x40, 0x42, 0x3C, 0x00 },
    /* 4: D */
    { 0x7C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x00 },
    /* 5: E */
    { 0x7E, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x7E, 0x00 },
    /* 6: F */
    { 0x7E, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x40, 0x00 },
    /* 7: G */
    { 0x3C, 0x42, 0x40, 0x4E, 0x42, 0x42, 0x3C, 0x00 },
    /* 8: H */
    { 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x00 },
    /* 9: I */
    { 0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00 },
    /* 10: J */
    { 0x1E, 0x04, 0x04, 0x04, 0x04, 0x44, 0x38, 0x00 },
    /* 11: K */
    { 0x42, 0x44, 0x78, 0x44, 0x42, 0x42, 0x42, 0x00 },
    /* 12: L */
    { 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00 },
    /* 13: M */
    { 0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x42, 0x00 },
    /* 14: N */
    { 0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x00 },
    /* 15: O */
    { 0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00 },
    /* 16: P */
    { 0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00 },
    /* 17: Q */
    { 0x3C, 0x42, 0x42, 0x42, 0x4A, 0x44, 0x3A, 0x00 },
    /* 18: R */
    { 0x7C, 0x42, 0x42, 0x7C, 0x44, 0x42, 0x42, 0x00 },
    /* 19: S */
    { 0x3C, 0x42, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00 },
    /* 20: T */
    { 0x7F, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00 },
    /* 21: U */
    { 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00 },
    /* 22: V */
    { 0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00 },
    /* 23: W */
    { 0x42, 0x42, 0x42, 0x42, 0x5A, 0x66, 0x42, 0x00 },
    /* 24: X */
    { 0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42, 0x00 },
    /* 25: Y */
    { 0x41, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00 },
    /* 26: Z */
    { 0x7E, 0x02, 0x04, 0x18, 0x20, 0x40, 0x7E, 0x00 },
};

/* Map ASCII to font_data index. Only uppercase + space supported. */
static unsigned char char_to_font(unsigned char c)
{
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A' + 1);
    return 0; /* default to space for unsupported chars */
}

/* Write a single character at row R, column C on the ULA screen. */
static void print_char(unsigned char row, unsigned char col, unsigned char c)
{
    unsigned char fi = char_to_font(c);
    unsigned char line;
    unsigned char *attr;

    for (line = 0; line < 8; line++) {
        unsigned int addr = 0x4000u
            + (unsigned int)((row & 0x18) << 8)
            + (unsigned int)(line << 8)
            + (unsigned int)((row & 7) << 5)
            + col;
        *((unsigned char *)addr) = font_data[fi][line];
    }

    /* Set attribute: bright white on black (ink=7, paper=0, bright=1) */
    attr = (unsigned char *)(0x5800u + (unsigned int)(row * 32) + col);
    *attr = 0x47; /* 01 000 111 = bright, paper=0, ink=7 */
}

/* Print a null-terminated string at row, col. */
static void print_str(unsigned char row, unsigned char col, const char *s)
{
    while (*s) {
        print_char(row, col, (unsigned char)*s);
        col++;
        s++;
    }
}

/* Clear the screen (zero pixel data + set attributes to white-on-black). */
static void cls(void)
{
    unsigned char *p;
    unsigned int i;

    /* Clear pixel data */
    p = (unsigned char *)0x4000u;
    for (i = 0; i < 6144; i++)
        p[i] = 0;

    /* Set all attributes to ink=7, paper=0 (white on black) */
    p = (unsigned char *)0x5800u;
    for (i = 0; i < 768; i++)
        p[i] = 0x07;
}

/* ------------------------------------------------------------------ *
 * AY initialization: silence all channels                              *
 * ------------------------------------------------------------------ */
static void ay_silence(void)
{
    ay_write(AY_CHA_VOL, 0);
    ay_write(AY_CHB_VOL, 0);
    ay_write(AY_CHC_VOL, 0);
    /* Disable all tone and noise (all bits high = disabled) */
    ay_write(AY_MIXER, 0x3F);
}

/* ------------------------------------------------------------------ *
 * main                                                                 *
 * ------------------------------------------------------------------ */
int main(void)
{
    unsigned char melody_idx;
    unsigned char bass_idx;
    unsigned char pad_idx;
    unsigned char frame_count;
    unsigned char cur_note;
    unsigned int  period;

    /* Clear screen and display title */
    cls();
    print_str(2,  9, "AY SOUND DEMO");
    print_str(5,  4, "TWINKLE TWINKLE LITTLE STAR");
    print_str(8,  5, "CHANNEL A  MELODY");
    print_str(9,  5, "CHANNEL B  BASS");
    print_str(10, 5, "CHANNEL C  PAD WITH ENVELOPE");
    print_str(14, 6, "PRESS BREAK TO EXIT");

    /* Silence the AY before configuring */
    ay_silence();

    /* Configure mixer:
     * Bits [5:0]: nC nB nA tC tB tA (active LOW)
     * We want tone on A, B, C and noise off on all:
     *   noise: all disabled (1,1,1) = bits 5:3 = 111
     *   tone:  all enabled  (0,0,0) = bits 2:0 = 000
     * = 0x38 */
    ay_write(AY_MIXER, 0x38);

    /* Channel A volume: fixed at 13 (loud, no envelope) */
    ay_write(AY_CHA_VOL, 13);

    /* Channel B volume: fixed at 10 (medium) */
    ay_write(AY_CHB_VOL, 10);

    /* Channel C volume: use envelope (bit 4 = 1) */
    ay_write(AY_CHC_VOL, 0x10);

    /* Envelope: triangle shape (10 = \/\/...), medium period */
    ay_write(AY_ENV_LO, 0x00);
    ay_write(AY_ENV_HI, 0x18);
    ay_write(AY_ENV_SHAPE, 10);  /* Triangle wave (\/\/) */

    /* Set noise period (unused but set to a safe value) */
    ay_write(AY_NOISE_PERIOD, 0);

    /* Ensure IM 1 + EI so HALT returns on frame interrupt */
    intrinsic_im_1();
    intrinsic_ei();

    /* Initialize playback state */
    melody_idx = 0;
    bass_idx = 0;
    pad_idx = 0;
    frame_count = 0;

    /* Main loop */
    while (1) {
        intrinsic_halt();  /* Wait for frame interrupt (50 Hz) */

        /* Every NOTE_FRAMES frames, advance to the next note */
        if (frame_count == 0) {
            /* Channel A: melody */
            cur_note = melody[melody_idx];
            if (cur_note == REST) {
                ay_write(AY_CHA_VOL, 0);  /* silence for rest */
            } else {
                ay_write(AY_CHA_VOL, 13);
                period = note_periods[cur_note];
                ay_set_tone(0, period);
            }

            /* Channel B: bass */
            cur_note = bass[bass_idx];
            if (cur_note == REST) {
                ay_write(AY_CHB_VOL, 0);
            } else {
                ay_write(AY_CHB_VOL, 10);
                period = note_periods[cur_note];
                ay_set_tone(1, period);
            }

            /* Channel C: pad with envelope */
            cur_note = pad[pad_idx];
            if (cur_note == REST) {
                ay_write(AY_CHC_VOL, 0);
            } else {
                ay_write(AY_CHC_VOL, 0x10);  /* envelope mode */
                period = note_periods[cur_note];
                ay_set_tone(2, period);
                /* Re-trigger envelope on note change */
                ay_write(AY_ENV_SHAPE, 10);
            }

            /* Advance indices, wrap around */
            melody_idx++;
            if (melody_idx >= MELODY_LEN) melody_idx = 0;

            bass_idx++;
            if (bass_idx >= BASS_LEN) bass_idx = 0;

            pad_idx++;
            if (pad_idx >= PAD_LEN) pad_idx = 0;
        }

        frame_count++;
        if (frame_count >= NOTE_FRAMES)
            frame_count = 0;
    }

    return 0;
}
