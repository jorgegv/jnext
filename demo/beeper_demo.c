/*
 * ZX Spectrum Next – ULA Beeper demo (plays a C major scale in a loop)
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 -subtype=nex beeper_demo.c -o beeper_demo -create-app
 *
 * Produces beeper_demo.nex loadable in CSpect / ZEsarUX / real hardware.
 *
 * Port 0xFE bit layout (from zxnext.vhd):
 *   bit 4   : EAR output (speaker toggle)
 *   bit 3   : MIC output
 *   bits 2:0: border colour
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <intrinsic.h>  /* intrinsic_halt(), intrinsic_ei() */

/* ------------------------------------------------------------------ *
 * Port I/O                                                            *
 * ------------------------------------------------------------------ */
__sfr __at 0xFE IO_ULA;

/* ------------------------------------------------------------------ *
 * Screen constants                                                    *
 *                                                                     *
 * ULA screen memory layout:                                           *
 *   Pixel data  : 0x4000 – 0x57FF (6144 bytes)                      *
 *   Attributes  : 0x5800 – 0x5AFF (768 bytes)                       *
 * ------------------------------------------------------------------ */
#define SCREEN_ATTR ((unsigned char *)0x5800)
#define SCREEN_PIX  ((unsigned char *)0x4000)

/* ------------------------------------------------------------------ *
 * 8x8 font for capital letters and space                              *
 *                                                                     *
 * We only need: B D E H I L M N O P R S (space)                      *
 * Stored as 8 bytes per glyph.                                        *
 * ------------------------------------------------------------------ */
static const unsigned char font_data[] = {
    /* ' ' (space) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 'B' */
    0x7C, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00,
    /* 'D' */
    0x78, 0x44, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00,
    /* 'E' */
    0x7E, 0x40, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00,
    /* 'H' */
    0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00,
    /* 'I' */
    0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00,
    /* 'L' */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00,
    /* 'M' */
    0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x42, 0x00,
    /* 'N' */
    0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x00,
    /* 'O' */
    0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00,
    /* 'P' */
    0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00,
    /* 'R' */
    0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x42, 0x00,
    /* 'S' */
    0x3C, 0x42, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00,
};

/* Index into font_data: map character to glyph offset */
static unsigned char font_index(char c)
{
    switch (c) {
        case ' ': return 0;
        case 'B': return 1;
        case 'D': return 2;
        case 'E': return 3;
        case 'H': return 4;
        case 'I': return 5;
        case 'L': return 6;
        case 'M': return 7;
        case 'N': return 8;
        case 'O': return 9;
        case 'P': return 10;
        case 'R': return 11;
        case 'S': return 12;
        default:  return 0;
    }
}

/* ------------------------------------------------------------------ *
 * Plot a single 8x8 character at character position (cx, cy)          *
 *                                                                     *
 * ULA pixel address for (cx, cy) in character coords:                 *
 *   byte = 0x4000 + (cy & 0x18)*256 + (row & 7)*256 + (cy & 7)*32   *
 *         + cx                                                        *
 * Simplified: use the standard Spectrum screen layout formula.        *
 * ------------------------------------------------------------------ */
static void plot_char(unsigned char cx, unsigned char cy, char c)
{
    unsigned char row;
    unsigned char glyph = font_index(c);
    const unsigned char *gdata = &font_data[glyph * 8];

    for (row = 0; row < 8; row++) {
        /* Compute pixel address for character row.
         * Standard Spectrum screen address:
         *   high byte: 010 bb rrr  where bb = cy>>3 (third), rrr = row (pixel row)
         *   low byte:  ccccc xxx   where ccccc = cy&7 (char row in third), xxx = cx
         * Wait — the standard formula:
         *   addr = 0x4000 | ((cy & 0x18) << 8) | (row << 8) | ((cy & 7) << 5) | cx
         */
        unsigned int addr = 0x4000u
            | ((unsigned int)(cy & 0x18) << 8)
            | ((unsigned int)row << 8)
            | ((unsigned int)(cy & 7) << 5)
            | cx;
        *((unsigned char *)addr) = gdata[row];
    }

    /* Set attribute: white ink on black paper, bright */
    SCREEN_ATTR[cy * 32 + cx] = 0x47; /* bright white ink, black paper */
}

/* ------------------------------------------------------------------ *
 * Print a string at character position (cx, cy)                       *
 * ------------------------------------------------------------------ */
static void print_at(unsigned char cx, unsigned char cy, const char *str)
{
    while (*str) {
        plot_char(cx, cy, *str);
        cx++;
        str++;
    }
}

/* ------------------------------------------------------------------ *
 * Clear screen: fill pixel area with 0, attributes with 0x47         *
 * ------------------------------------------------------------------ */
static void cls(void)
{
    unsigned int i;
    for (i = 0; i < 6144; i++)
        SCREEN_PIX[i] = 0;
    for (i = 0; i < 768; i++)
        SCREEN_ATTR[i] = 0x47;
}

/* ------------------------------------------------------------------ *
 * Beeper tone generation                                              *
 *                                                                     *
 * To generate a square wave at frequency F:                           *
 *   half_period = CPU_FREQ / (2 * F) T-states                        *
 * At 3.5 MHz, a busy loop iteration takes roughly 24 T-states        *
 * (DJNZ-style inner loop), so:                                       *
 *   loop_count = half_period / ~24                                    *
 *                                                                     *
 * We use inline assembly for precise timing.                          *
 * ------------------------------------------------------------------ */

/* Delay approximately 'count' iterations of a tight loop.
 * Each iteration: DEC BC (6T) + LD A,B (4T) + OR C (4T) + JR NZ (12T/7T)
 * ~26 T-states per iteration when taken.
 */
static void delay_loop(unsigned int count) __naked
{
    (void)count;
    __asm
        ; count is in HL (sdcc_iy calling convention, first arg in HL)
        ld   b, h
        ld   c, l
    _delay_inner:
        dec  bc             ; 6T
        ld   a, b           ; 4T
        or   a, c           ; 4T
        jr   nz, _delay_inner  ; 12T (taken) / 7T (not taken)
        ret
    __endasm;
}

/* Play a tone.
 *   half_period_loops: number of delay loop iterations for half a wave period
 *   num_cycles: number of full wave cycles to play (determines duration)
 *   border_colour: border colour (bits 2:0)
 */
static void play_tone(unsigned int half_period_loops,
                      unsigned int num_cycles,
                      unsigned char border_colour)
{
    unsigned int i;
    unsigned char ear_on  = 0x10 | (border_colour & 0x07);  /* bit 4 = EAR on  */
    unsigned char ear_off = 0x00 | (border_colour & 0x07);  /* bit 4 = EAR off */

    for (i = 0; i < num_cycles; i++) {
        IO_ULA = ear_on;
        delay_loop(half_period_loops);
        IO_ULA = ear_off;
        delay_loop(half_period_loops);
    }
}

/* Silence (pause) for a given number of delay iterations */
static void silence(unsigned int loops)
{
    IO_ULA = 0x00;  /* EAR off, border black */
    delay_loop(loops);
}

/* ------------------------------------------------------------------ *
 * Note table                                                          *
 *                                                                     *
 * Half-period in delay loop iterations (~26 T-states each):           *
 *   loops = 3500000 / (2 * freq * 26)                                *
 *                                                                     *
 * Note  Freq(Hz)  Half-period(T)  Loops(~26T)                        *
 * C4    262       6679            257                                 *
 * D4    294       5952            229                                 *
 * E4    330       5303            204                                 *
 * F4    349       5014            193                                 *
 * G4    392       4464            172                                 *
 * A4    440       3977            153                                 *
 * B4    494       3543            136                                 *
 * C5    523       3346            129                                 *
 * ------------------------------------------------------------------ */

#define NOTE_C4  257
#define NOTE_D4  229
#define NOTE_E4  204
#define NOTE_F4  193
#define NOTE_G4  172
#define NOTE_A4  153
#define NOTE_B4  136
#define NOTE_C5  129
#define NOTE_REST  0   /* silence marker */

/* Duration: number of full wave cycles for ~0.25 seconds at each freq.
 * cycles = freq * duration_seconds
 * For quarter note at each pitch:
 *   C4: 262*0.25 = 66
 *   D4: 294*0.25 = 74
 *   etc.
 */
#define DUR_C4   66
#define DUR_D4   74
#define DUR_E4   83
#define DUR_F4   87
#define DUR_G4   98
#define DUR_A4  110
#define DUR_B4  124
#define DUR_C5  131

/* Melody: C major scale up and down */
static const unsigned int melody_notes[] = {
    NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4,
    NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5,
    NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4,
    NOTE_F4, NOTE_E4, NOTE_D4, NOTE_C4,
};

static const unsigned int melody_durations[] = {
    DUR_C4, DUR_D4, DUR_E4, DUR_F4,
    DUR_G4, DUR_A4, DUR_B4, DUR_C5,
    DUR_C5, DUR_B4, DUR_A4, DUR_G4,
    DUR_F4, DUR_E4, DUR_D4, DUR_C4,
};

/* Border colours cycle through for visual effect */
static const unsigned char note_colours[] = {
    1, 2, 3, 4, 5, 6, 7, 2,
    2, 7, 6, 5, 4, 3, 2, 1,
};

#define MELODY_LEN 16

/* Gap between notes (in delay loop iterations) — ~20ms */
#define NOTE_GAP  2700

/* Gap between melody repeats — ~0.5s */
#define MELODY_GAP  27000

/* ------------------------------------------------------------------ *
 * Additional display characters needed: 'A', 'C', 'G', '-'           *
 * We'll add them to the font and extend print support.                *
 * Actually, let's keep it simple and only use the chars we defined.   *
 * "BEEPER DEMO" needs: B E P R space D M O                           *
 * "PRESS SPACE" needs: P R E S space — already have all.             *
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ *
 * main                                                                *
 * ------------------------------------------------------------------ */
int main(void)
{
    unsigned char i;

    /* Set up IM 1 + EI for HALT-based frame sync */
    intrinsic_im_1();
    intrinsic_ei();

    /* Clear screen */
    cls();

    /* Display title */
    print_at(11, 3, "BEEPER DEMO");

    /* Display note names */
    print_at(5, 8, "SIMPLE MELODIE");

    /* Display instruction */
    print_at(7, 20, "PRESS S OR HOLD");

    /* Set border to black */
    IO_ULA = 0x00;

    /* Main loop: play the scale melody repeatedly */
    while (1) {
        /* Play all notes in the melody */
        for (i = 0; i < MELODY_LEN; i++) {
            play_tone(melody_notes[i], melody_durations[i],
                      note_colours[i]);
            /* Brief silence between notes for articulation */
            silence(NOTE_GAP);
        }

        /* Longer pause between melody repetitions */
        silence(MELODY_GAP);
    }

    return 0;
}
