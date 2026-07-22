/*
 * ZX Spectrum Next -- LoRes 8-bit mode screenshot fixture (GH #63)
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 -subtype=nex lores_demo.c \
 *       -o lores_demo -create-app
 *
 * NOT a demo -- a regression fixture. LoRes shipped with 88 unit / compositor
 * / NextREG rows and ZERO screenshot rows, so nothing in the suite rendered a
 * LoRes frame at all: the whole ULA-slot substitution path could break
 * silently. This is the smallest program that pins it.
 *
 * Plan: doc/testing/LORES-TEST-PLAN-DESIGN.md ("Test Architecture", tier S).
 *
 *
 * WHY THE IMAGE IS PAINTED FROM CODE AND NOT LOADED AS A NEX SCREEN BLOCK
 * ----------------------------------------------------------------------
 * A NEX SCREEN_LORES header block would be the obvious way to get a LoRes
 * image on screen, and it is exactly the wrong way here. GH #68:
 * src/core/nex_loader.cpp loads that block as 12288 CONTIGUOUS bytes at
 * bank-5 offset 0, where the official loader (tbblue nexload.asm:472-474)
 * issues two 6144-byte reads, to $4000 and then to $6000. A misplaced second
 * half looks IDENTICAL to a missing `+1` on lores_addr(13:11) -- bottom 48
 * rows wrong, top 48 right is the signature of both -- so a fixture built on
 * that path could bless the wrong behaviour, and would break for the wrong
 * reason when #68 lands. Painting from Z80 code isolates the RENDERER, which
 * is what this row exists to test.
 *
 *
 * WHAT IS ON SCREEN, AND WHY IT MUST LOOK LIKE THAT
 * -------------------------------------------------
 * 8-bit LoRes mode (NR $6A = 0), scroll 0 (NR $32 = NR $33 = 0), palette
 * offset 0, the NR $1A clip window at its full-screen reset value.
 *
 * The bank-5 byte for LoRes pixel (cx, cy), cx in [0,127], cy in [0,95], is
 *
 *      byte(cx, cy) = ((cy >> 3) << 4) | (cx >> 3)
 *
 * i.e. a 16-column x 12-row grid of cells, each cell 8x8 LoRes pixels and
 * each cell carrying a UNIQUE byte value 0x00..0xBF. Uniqueness is the point:
 * any address-generation error moves a cell and changes the image.
 *
 * The ULA palette is loaded with the IDENTITY map, palette[i] = i as
 * RRRGGGBB. lores.vhd:102/111 makes the emitted index
 * `((data(7:4) + offset) & 0xF) : data(3:0)`, which at offset 0 is the byte
 * itself, so with an identity palette the displayed colour is a closed-form
 * function of the byte -- and therefore of the screen position:
 *
 *      colour(cx, cy) = expand8( ((cy >> 3) << 4) | (cx >> 3) )
 *
 * Each LoRes pixel is a 2x2 block of display pixels (lores.vhd:91 indexes
 * x(7:1) and y(7:1)), so cell (g, b) covers display columns 16g..16g+15 and
 * display rows 16b..16b+15 of the 256x192 display area. That is the whole
 * expected frame, derived on paper from the VHDL before any render existed.
 * derive-lores-reference.py in this directory recomputes that expectation
 * independently, from the VHDL formulas only, and diffs it against the
 * committed reference PNG.
 *
 * Index 0xE3 is never emitted (the used set is 0x00..0xBF): 0xE3 is the NR
 * $14 global transparency key, and with an identity palette that one entry
 * would be transparent.
 *
 *
 * THE HALF-SKIP, WHICH IS THE ROW'S SHARPEST ASSERTION
 * ----------------------------------------------------
 * lores.vhd:93-94 adds 1 to addr(13:11) when y >= 96, so LoRes rows 0..47
 * live at bank-5 0x0000-0x17FF ($4000-$57FF) and rows 48..95 at
 * 0x2000-0x37FF ($6000-$77FF), skipping the 0x1800-0x1FFF attribute area
 * (LR-45, LR-47).
 *
 * So the two halves are written to two DISCONTIGUOUS CPU ranges here, and
 * the 2K gap is filled with a marker byte (0xFF) that is outside the used
 * index range. Drop the `+1` and rows 48..63 read the marker (solid white,
 * display rows 96..127) while rows 64..95 read the content of rows 48..79
 * (the whole bottom half shifted up by 16 LoRes rows). Both are enormous,
 * unmistakable pixel diffs.
 *
 * (The ROM's IM1 handler keeps system variables at $5B00-$5FFF, inside the
 * gap. That is harmless: a correct LoRes never reads the gap -- LR-47 -- so
 * the reference frame is fully deterministic. Only a BROKEN renderer sees
 * those bytes, and it fails either way.)
 *
 *
 * NO LORES => AN OBVIOUSLY DIFFERENT SCREEN
 * -----------------------------------------
 * Bank 5 is also the classic ULA screen, so if the NR $15 bit 7 enable gate
 * is lost the ULA renders $4000 as a bitmap with $5800 as attributes. The
 * marker 0xFF is FLASH+BRIGHT+paper 7+ink 7, so the frame collapses to
 * flashing white -- nothing like the colour grid.
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

#define NR_TRANSPARENT     0x14   /* global transparent colour (RRRGGGBB)   */
#define NR_LAYER_CTRL      0x15   /* bit7 = LoRes enable, bits 4:2 priority */
#define NR_CLIP_ULA        0x1A   /* shared ULA/LoRes clip window           */
#define NR_CLIP_IDX_RESET  0x1C   /* bit2 resets the NR $1A write index     */
#define NR_LORES_SCROLLX   0x32
#define NR_LORES_SCROLLY   0x33
#define NR_PALETTE_IDX     0x40
#define NR_PALETTE_VAL     0x41
#define NR_PALETTE_CTRL    0x43   /* bits 6:4 write-target, bit1 ULA bank   */
#define NR_FALLBACK        0x4A   /* shown when every layer is transparent  */
#define NR_ULA_CTRL        0x68   /* bit7 ULA disable, bit0 stencil         */
#define NR_DISPLAY_CTRL    0x69   /* bit7 = Layer 2 enable                  */
#define NR_LORES_CTRL      0x6A   /* b5 radastan, b4 dfile xor, b3:0 offset */
#define NR_TILEMAP_CTRL    0x6B   /* bit7 = tilemap enable                  */

#define PAL_ULA_FIRST      0x00   /* NR $43 bits 6:4 = 000 => ULA palette 1 */

/* Bank 5 is mapped at $4000-$7FFF, so bank-5 offset N is at $4000 + N. */
#define LORES_TOP     ((unsigned char *)0x4000)  /* offset 0x0000, rows 0..47  */
#define LORES_GAP     ((unsigned char *)0x5800)  /* offset 0x1800, never read  */
#define LORES_BOTTOM  ((unsigned char *)0x6000)  /* offset 0x2000, rows 48..95 */

#define GAP_MARKER    0xFF        /* outside the used index range 0x00..0xBF */

#define LORES_W       128
#define LORES_H       96
#define HALF_ROWS     48

/* Paint the 16x12 cell grid, each cell one unique byte 0x00..0xBF. */
static void paint_lores(void)
{
    unsigned char *p;
    unsigned char  band;
    unsigned char  cy, cx;

    for (cy = 0; cy < LORES_H; cy++) {
        band = (unsigned char)((cy >> 3) << 4);
        p = (cy < HALF_ROWS)
              ? LORES_TOP    + (unsigned int)cy * LORES_W
              : LORES_BOTTOM + (unsigned int)(cy - HALF_ROWS) * LORES_W;
        for (cx = 0; cx < LORES_W; cx++)
            p[cx] = (unsigned char)(band | (cx >> 3));
    }

    memset(LORES_GAP, GAP_MARKER, 2048);
}

/*
 * ULA palette entry i = RRRGGGBB value i, for all 256 entries.
 *
 * NR $40 is rewritten before every NR $41 on purpose: the hardware
 * auto-increments the index on an 8-bit palette write, but this fixture's
 * whole value rests on the palette being exactly the identity, so it does not
 * lean on that.
 */
static void identity_ula_palette(void)
{
    unsigned int i;

    ZXN_WRITE_REG(NR_PALETTE_CTRL, PAL_ULA_FIRST);
    for (i = 0; i < 256; i++) {
        ZXN_WRITE_REG(NR_PALETTE_IDX, (unsigned char)i);
        ZXN_WRITE_REG(NR_PALETTE_VAL, (unsigned char)i);
    }
}

int main(void)
{
    intrinsic_di();

    paint_lores();
    identity_ula_palette();

    /* Every compositor input the capture depends on, pinned. */
    ZXN_WRITE_REG(NR_TRANSPARENT, 0xE3);   /* global transparent key (default) */
    ZXN_WRITE_REG(NR_FALLBACK,    0x00);   /* black -- must never be visible   */
    ZXN_WRITE_REG(NR_DISPLAY_CTRL, 0x00);  /* Layer 2 off                      */
    ZXN_WRITE_REG(NR_TILEMAP_CTRL, 0x00);  /* tilemap off                      */
    ZXN_WRITE_REG(NR_ULA_CTRL,     0x00);  /* ULA enabled, no stencil/blend    */

    /* The shared ULA/LoRes clip window (NR $1A), explicitly at its full
     * 256x192 reset value: reset the rotating write index via NR $1C bit 2,
     * then X1, X2, Y1, Y2. LoRes has no clip register of its own
     * (zxnext.vhd:4258-4261; NR $1D is undecoded). */
    ZXN_WRITE_REG(NR_CLIP_IDX_RESET, 0x04);
    ZXN_WRITE_REG(NR_CLIP_ULA, 0x00);
    ZXN_WRITE_REG(NR_CLIP_ULA, 0xFF);
    ZXN_WRITE_REG(NR_CLIP_ULA, 0x00);
    ZXN_WRITE_REG(NR_CLIP_ULA, 0xBF);

    /* LoRes: 8-bit mode, no dfile XOR, palette offset 0, no scroll. */
    ZXN_WRITE_REG(NR_LORES_SCROLLX, 0x00);
    ZXN_WRITE_REG(NR_LORES_SCROLLY, 0x00);
    ZXN_WRITE_REG(NR_LORES_CTRL,    0x00);

    /* Pin the border. A LoRes pixel must never reach it: lores.vhd:115 clips
     * the module output to phc in [0,255] and vc in [0,191] all by itself
     * (plan row LR-22), so the border staying ONE uniform colour is part of
     * what this capture asserts.
     *
     * Border colour n indexes the PAPER half of the ULA palette (entries
     * 0x10..0x1F), not the ink half, so under the identity palette border 0
     * comes out as RRRGGGBB 0x10 -- a mid-green, not black. That is correct
     * and deterministic; it is spelled out here so a reader of the reference
     * PNG does not mistake it for a bug. */
    zx_border(0);

    /* LoRes ON. Bits 4:2 = 000 (priority SLU), bits 1:0 = 00 (sprites off). */
    ZXN_WRITE_REG(NR_LAYER_CTRL, 0x80);

    intrinsic_im_1();
    intrinsic_ei();

    while (1)
        intrinsic_halt();       /* static frame */
}
