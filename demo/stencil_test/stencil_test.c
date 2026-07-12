/*
 * ZX Spectrum Next -- Tilemap stencil-mode test fixture (Task 22b)
 *
 * Build with z88dk (zxn target):
 *
 *   zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 -subtype=nex stencil_test.c -o stencil_test -create-app
 *
 * NOT a demo — a regression fixture. No committed NEX in test/00regression/nex/
 * drove ULA stencil mode (verified: zero NR 0x68 bit-0 writes across all of
 * them), so the compositor's stencil branch had no screenshot-level coverage
 * at all. This is the smallest program that lights it up: an opaque ULA pixel
 * under an opaque tile pixel, with stencil mode on.
 *
 * VHDL zxnext.vhd:7130 selects the stencil AND-branch only when BOTH enables
 * are set:
 *
 *   if ula_stencil_mode_2 = '1' and ula_en_2 = '1' and tm_en_2 = '1' then
 *      ula_final_rgb <= stencil_rgb;      -- ula_rgb AND tm_rgb   (7113)
 *   else
 *      ula_final_rgb <= ulatm_rgb;        -- ordinary merge       (7135)
 *
 * and stencil_rgb is transparent whenever EITHER input is (7112). So excluding
 * either layer with --delayed-screenshot-layers must drop OUT of the
 * AND-branch and show the survivor. A compositor that gates stencil on only
 * one of the two enables erases the survivor into the NR 0x4A fallback
 * instead — which is exactly the bug this fixture pins.
 *
 * Screen composition (deliberately STATIC, so any capture frame is equivalent
 * and the screenshot is deterministic):
 *   ULA     - full-screen vertical stripes, all-ink pixels, bright
 *   Tilemap - 4x4-tile checkerboard: solid RED tiles over transparent tiles
 *   Stencil - the red tiles act as a colour filter over the ULA stripes.
 *   NR 0x4A - forced to black, so a dropped layer is unmistakable.
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#include <arch/zxn.h>
#include <intrinsic.h>
#include <string.h>

#define NR_SPRITE_CTRL      0x15   /* layer priority + sprite enable        */
#define NR_TRANSPARENT      0x14   /* global transparent colour (RRRGGGBB)  */
#define NR_PALETTE_IDX      0x40
#define NR_PALETTE_VAL      0x41
#define NR_PALETTE_CTRL     0x43   /* bits[6:4] = palette select            */
#define NR_FALLBACK         0x4A   /* shown when every layer is transparent */
#define NR_TILEMAP_TRANSP   0x4C   /* tilemap transparent pixel index       */
#define NR_ULA_CTRL         0x68   /* bit7 = ULA disable, bit0 = stencil    */
#define NR_DISPLAY_CTRL     0x69   /* bit7 = Layer 2 enable                 */
#define NR_TILEMAP_CTRL     0x6B   /* bit7 = tilemap enable                 */
#define NR_TILEMAP_DEFATTR  0x6C
#define NR_TILEMAP_MAPBASE  0x6E
#define NR_TILEMAP_DEFBASE  0x6F

#define PAL_TILEMAP_FIRST   0x30   /* NR 0x43 bits[6:4] = 011 => tilemap    */

/*
 * Bank 5 is mapped at 0x4000..0x7FFF. The ULA screen occupies
 * 0x4000..0x5AFF, so — unlike tilemap_demo, which overlays the tilemap ON
 * the screen area — the tilemap structures must live ABOVE it: this fixture
 * needs BOTH layers carrying real content at the same time.
 *
 *   map   @ 0x6000 = bank5 offset 0x2000 -> NR 0x6E = 0x2000/256 = 0x20
 *   tiles @ 0x6C00 = bank5 offset 0x2C00 -> NR 0x6F = 0x2C00/256 = 0x2C
 *
 * Map is 40*32*2 = 2560 (0xA00) bytes: 0x6000..0x69FF. Patterns are
 * 2 tiles * 32 bytes = 64 bytes at 0x6C00. Neither overlaps the screen.
 */
#define ULA_PIXELS      0x4000
#define ULA_ATTRS       0x5800
#define TILEMAP_ADDR    0x6000
#define PATTERNS_ADDR   0x6C00

#define TM_COLS         40
#define TM_ROWS         32

#define PX_TRANSPARENT  0x0F   /* matches the NR 0x4C default (0x0F) */
#define PX_SOLID        0x01   /* -> tilemap palette entry 1 (red)   */

#define TILE_TRANSPARENT 0
#define TILE_SOLID       1

#define PX(a,b) (unsigned char)(((a)<<4)|(b))

/* Two 8x8 4bpp tiles, 32 bytes each: one fully transparent, one solid. */
static const unsigned char patterns[2 * 32] = {
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),
    PX(PX_TRANSPARENT, PX_TRANSPARENT), PX(PX_TRANSPARENT, PX_TRANSPARENT),

    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
    PX(PX_SOLID, PX_SOLID), PX(PX_SOLID, PX_SOLID),
};

static void pal_write(unsigned char idx, unsigned char rrrgggbb)
{
    ZXN_WRITE_REG(NR_PALETTE_IDX, idx);
    ZXN_WRITE_REG(NR_PALETTE_VAL, rrrgggbb);
}

/*
 * ULA: all-ink pixels + vertical stripes of bright ink.
 *
 * Ink 3 is skipped on purpose: BRIGHT magenta is RRRGGGBB 0xE3, which is the
 * NR 0x14 default transparent colour — those pixels would read as transparent
 * and muddy the stencil result.
 */
static void draw_ula(void)
{
    unsigned char *attr;
    unsigned char  row, col;
    static const unsigned char inks[5] = { 1, 2, 4, 5, 6 };  /* no 3 */

    memset((void *)ULA_PIXELS, 0xFF, 6144);   /* every pixel = ink */

    attr = (unsigned char *)ULA_ATTRS;
    for (row = 0; row < 24; row++) {
        for (col = 0; col < 32; col++) {
            *attr++ = (unsigned char)(0x40 | inks[col % 5]);  /* bright + ink */
        }
    }
}

/* Tilemap: 4x4-tile checkerboard of solid tiles over transparent tiles. */
static void fill_tilemap(void)
{
    unsigned char *map = (unsigned char *)TILEMAP_ADDR;
    unsigned char  row, col;

    for (row = 0; row < TM_ROWS; row++) {
        for (col = 0; col < TM_COLS; col++) {
            *map++ = (((row >> 2) + (col >> 2)) & 1) ? TILE_SOLID
                                                     : TILE_TRANSPARENT;
            *map++ = 0x00;   /* palette offset 0, no mirror/rotate */
        }
    }
}

int main(void)
{
    memcpy((void *)PATTERNS_ADDR, patterns, sizeof(patterns));
    fill_tilemap();
    draw_ula();

    /* Tilemap palette entry 1 = pure red. Under stencil this ANDs with the
     * ULA colour, so the tiles behave as a red filter over the stripes. */
    ZXN_WRITE_REG(NR_PALETTE_CTRL, PAL_TILEMAP_FIRST);
    pal_write(1, 0xE0);                     /* 111_000_00 = red */

    /* Pin every compositor input the capture depends on. */
    ZXN_WRITE_REG(NR_TRANSPARENT, 0xE3);    /* global transparent key         */
    ZXN_WRITE_REG(NR_TILEMAP_TRANSP, 0x0F); /* tilemap transparent index      */
    ZXN_WRITE_REG(NR_FALLBACK, 0x00);       /* fallback = black               */
    ZXN_WRITE_REG(NR_SPRITE_CTRL, 0x00);    /* sprites off, priority SLU      */
    ZXN_WRITE_REG(NR_DISPLAY_CTRL, 0x00);   /* Layer 2 off                    */

    ZXN_WRITE_REG(NR_TILEMAP_MAPBASE, 0x20);
    ZXN_WRITE_REG(NR_TILEMAP_DEFBASE, 0x2C);
    ZXN_WRITE_REG(NR_TILEMAP_DEFATTR, 0x00);
    ZXN_WRITE_REG(NR_TILEMAP_CTRL, 0x80);   /* tilemap enable, 40-col         */

    /* Stencil ON (bit 0). Bit 7 stays 0, i.e. ULA ENABLED — the AND-branch at
     * VHDL 7130 needs ula_en=1 AND tm_en=1. */
    ZXN_WRITE_REG(NR_ULA_CTRL, 0x01);

    intrinsic_im_1();
    intrinsic_ei();

    while (1) {
        intrinsic_halt();   /* static frame */
    }

    return 0;
}
