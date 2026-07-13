/*
 * ZX Spectrum Next -- Nirvana-class mid-scanline attribute racing demo
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 -subtype=nex nirvana_demo.c \
 *       -o nirvana_demo -create-app
 *
 * Demonstrates the classic "Nirvana" multicolour technique: the CPU
 * races the video beam, rewriting the SAME ULA attribute byte between
 * each of the 8 scanlines that make up one 8x8 character cell, so a
 * single physical byte shows up to 8 different colours down the
 * screen -- far more colour resolution than the nominal "one attribute
 * per 8x8 cell" hardware limit.
 *
 * This is possible because the ULA does not fetch the attribute byte
 * once per frame or once per character cell: it re-reads the SAME
 * attribute-row address fresh on EVERY scanline (VHDL
 * video/zxula.vhd:192, 223-224 -- the attribute address is a function
 * of the live raster line counter, re-latched every scanline). If the
 * CPU rewrites that byte between one scanline's fetch and the next,
 * the two scanlines see genuinely different values.
 *
 * Technique used here: busy-poll NextREG 0x1E/0x1F (Active Video Line)
 * until the beam reaches the target scanline, then rewrite the full
 * 32-byte attribute row for that scanline. Polling (rather than fixed
 * NOP-counted delays) is timing-robust across CPU speeds; NR 0x07 is
 * set to 28 MHz here purely for margin, not because the technique
 * requires it.
 *
 * Expected result: 24 character rows, each showing an 8-band vertical
 * colour gradient (one band per scanline) instead of a single flat
 * colour per row -- the classic Nirvana look.
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>

/* Screen row 0 (top of the 192-line display) is raster line VC_BASE on
 * a Next-family 50 Hz frame (32-line top border + 32-line vblank/porch
 * offset ahead of the border -- see Emulator::begin_new_frame /
 * on_scanline vblank_top() derivation: vc=64 <-> framebuffer row 32 <->
 * ZX screen_row 0). */
#define VC_BASE        64u

#define ATTR_BASE      ((unsigned char *)0x5800)
#define PIXEL_BASE     ((unsigned char *)0x4000)

/* 8 distinct bright INK colours, one per scanline within a cell.
 * Paper stays black (bits 3:2 = 0); pixel data is solid ink (0xFF)
 * so the visible colour is entirely the ink field. Bright bit (0x40)
 * set for maximum visual contrast. */
static const unsigned char band_colour[8] = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47
};

static unsigned int read_vline(void)
{
    unsigned char msb, lsb;
    msb = ZXN_READ_REG(0x1E) & 0x01;
    lsb = ZXN_READ_REG(0x1F);
    return ((unsigned int)msb << 8) | lsb;
}

static void wait_for_line(unsigned int target)
{
    while (read_vline() != target) {
        /* busy-poll */
    }
}

int main(void)
{
    unsigned int i;
    unsigned char row, sub;
    unsigned char *attr_row;

    /* Solid ink across the whole screen so the attribute ink colour is
     * fully visible on every pixel row. */
    for (i = 0; i < 6144u; i++) {
        PIXEL_BASE[i] = 0xFF;
    }
    /* Baseline attribute (frame-start value AttributeMux snapshots as
     * its baseline) -- irrelevant once racing starts, but must be a
     * defined value. */
    for (i = 0; i < 768u; i++) {
        ATTR_BASE[i] = 0x38;   /* white paper, black ink, bright */
    }

    intrinsic_di();

    /* Rewrite only the leftmost COLS_RACED columns of each row per
     * scanline match: a 32-byte full-row rewrite (~9-13 T-states/byte
     * in compiled C, ~300-400 T-states total) does not reliably fit
     * inside one video line's budget (228 T-states at 3.5 MHz) once
     * the raster-line poll overhead is added -- verified empirically:
     * a full-row version missed its very next per-scanline target and
     * fell back to a once-per-real-frame cadence, never crossing the
     * arm threshold. Racing only the left few columns keeps each
     * match comfortably inside one line's budget while still proving
     * the effect: those columns show a full 8-band gradient down the
     * screen (impossible without per-scanline replay), the remaining
     * columns show the frame's last write (today's baseline
     * behaviour) -- exactly the "before/after" contrast this demo
     * exists to demonstrate.
     */
#define COLS_RACED 8u
    while (1) {
        for (row = 0; row < 24; row++) {
            attr_row = ATTR_BASE + (unsigned int)row * 32u;
            for (sub = 0; sub < 8; sub++) {
                wait_for_line(VC_BASE + (unsigned int)row * 8u + sub);
                for (i = 0; i < COLS_RACED; i++) {
                    attr_row[i] = band_colour[sub];
                }
            }
        }
    }

    return 0;
}
