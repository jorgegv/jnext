#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"
#include "palette_manager.h"
#include "palette_data.h"
#include <util_next.h>
#include "tilemap_manager.h"
#include <keyb.h>

#include <string.h>

#include "screen_list.h"





#include <im2.h>
#include <z80.h>
#include <intrinsic.h>
#include <string.h>
#include <arch/zxn.h>

#include <interrupt.h>

/*

Memory map

FE01 - FFFF (511 bytes) Stack space
FD00 - FE00 (257 bytes) IM2 service routine pointer (all FC bytes)
FCFC - FCFF (  4 bytes) ISR jump - JP nn [3 bytes]; NOP


*/

void onInterrupt(void);

u16 first_sp;

void main(void)
{
    first_sp = get_sp();

    clip_ula(255, 255, 255, 255);

    clip_ctr_layer2(320, 256);

    layer2_init();

    // 000 010 011 -> 0000 1001 1 -> 0000 1001 0000 0001
    layer2_set_first_palette_entry(0x0901);
    layer2_clear_pages();

    // sprite layers and system register $15c
    // https://wiki.specnext.dev/Sprite_and_LAYERers_System_Register
    // clip-over-border, SUL, sprites-over-border, sprites-visible
    ZXN_NEXTREG(0x15, 0x2B);

    ZXN_NEXTREG(0x4B, 0x0F);

    palette_load_tilemap(tileset_palette);
    tilemap_setup();
    tilemap_clear_buf_value(' ');
    clip_ctr_tilemap(320, 256);


    keyb_init();

    // sc_switch_screen(sm_init, sm_update, NULL);
    sc_switch_screen(s0_init, s0_update, NULL);
    // sc_switch_screen(s1_init, s1_update, NULL);
    // sc_switch_screen(s2_init, s2_update, NULL);
    // sc_switch_screen(s3_init, s3_update, NULL);
    // sc_switch_screen(s4_init, s4_update, NULL);

    // 208 - 216 - 224

    on_interrupt_callback = onInterrupt;
    interrupt_init_FD();
    // interrupt_init_FE();
    interrupt_setup_scanline_only(208);

    while(true) {
        // waitForScanline(223);
        // keyb_update();
        // sc_update();
    }
}

void onInterrupt(void)
{
    s0_interrupt();
    waitForScanline(224);
    // s0_end_screen();
    keyb_update();
    sc_update();
}

