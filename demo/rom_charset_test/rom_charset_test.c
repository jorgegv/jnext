/*
 * ROM Character-Set Test (Task 20 regression demo)
 *
 * Reads the ZX BASIC ROM character set at $3D00 (where characters '
 * onwards live as 8 bytes each) and blits the first 64 chars worth of
 * data (512 bytes) onto the screen at the top.
 *
 * Why this test exists:
 *   --machine next --load <TAP> bypasses tbblue.fw's `load_roms()` and
 *   used to leave SRAM pages 2,3 (the 1-bit sram_rom = 1 selection)
 *   empty (0xFF). 128K-class games that select ROM 1 via
 *   `out (0x7FFD), 0x10` would then read 0xFF for every ROM byte —
 *   including the character set at $3D00 — producing solid blocks
 *   instead of recognisable text whenever SP1 or any glyph-from-ROM
 *   path was used (Cesare / RAGE1 / many z88dk games).
 *
 * Detection in the regression suite: this demo copies $3D00..$3EFF to
 * screen at y=0..31, attribute INK 7 / PAPER 0 / BRIGHT. With the bug
 * present, the entire top 4 character rows render as solid-bright-white
 * blocks (every pixel 0xFF). Post-fix, they render as the canonical
 * Spectrum font glyphs " !"#$..." etc., which the reference PNG
 * captures.
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <intrinsic.h>

#define SCREEN_ATTR ((unsigned char *)0x5800)
#define SCREEN_PIX  ((unsigned char *)0x4000)
#define ROM_CHARSET ((unsigned char *)0x3D00)

/* Plot one 8x8 ROM-character glyph at character coords (cx, cy). The
 * glyph index `g` selects bytes ROM_CHARSET[g*8 .. g*8+7], i.e. ROM
 * char ' ' + g.
 */
static void plot_rom_glyph(unsigned char cx, unsigned char cy, unsigned char g)
{
    unsigned char row;
    const unsigned char *gdata = &ROM_CHARSET[g * 8];

    for (row = 0; row < 8; row++) {
        unsigned int addr = 0x4000u
            | ((unsigned int)(cy & 0x18) << 8)
            | ((unsigned int)row << 8)
            | ((unsigned int)(cy & 7) << 5)
            | cx;
        *((unsigned char *)addr) = gdata[row];
    }
    SCREEN_ATTR[cy * 32u + cx] = 0x47;  /* INK 7 PAPER 0 BRIGHT */
}

static void cls_to_black(void)
{
    unsigned int i;
    for (i = 0; i < 6144; i++) SCREEN_PIX[i]  = 0;
    for (i = 0; i < 768;  i++) SCREEN_ATTR[i] = 0x07;  /* white-on-black no bright */
}

/* Set port 0x7FFD to USR0 mode: bank 0 mapped at $C000, 48K BASIC ROM
 * (port_7ffd bit 4 = 1) at slot 0/1. This mirrors what RAGE1's
 * bswitch (and most 128K-class games' init) does: pages the right RAM
 * bank and selects ROM 1. With the Task 20 fix, sram_rom = 1 in Next
 * mode maps to SRAM pages 2,3 which now hold the 48K BASIC.
 */
static void set_basic_rom(void) __naked
{
    __asm
        ld   a, 0x10        ; bit 4 = 1 (ROM 1 select), bank 0 at $C000
        ld   bc, 0x7FFD
        out  (c), a
        ret
    __endasm;
}

int main(void)
{
    unsigned char cx, cy, g;

    intrinsic_di();
    cls_to_black();
    set_basic_rom();

    /* Plot 32 columns × 4 rows = 128 ROM glyphs starting from char ' '.
     * Each row of cells gets 32 consecutive char-set glyphs (g = 0..127).
     */
    g = 0;
    for (cy = 0; cy < 4; cy++) {
        for (cx = 0; cx < 32; cx++) {
            plot_rom_glyph(cx, cy, g);
            g++;
        }
    }

    /* Halt forever in IM 1 so the screen stays visible for the
     * screenshot.
     */
    intrinsic_im_1();
    intrinsic_ei();
    while (1) {
        intrinsic_halt();
    }

    return 0;
}
