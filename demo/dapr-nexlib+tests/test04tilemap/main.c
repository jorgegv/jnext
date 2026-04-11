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
 
void main(void)
{
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

    sc_switch_screen(sm_init, sm_update, NULL);
    // sc_switch_screen(s1_init, s1_update, NULL);
    // sc_switch_screen(s2_init, s2_update, NULL);
    // sc_switch_screen(s3_init, s3_update, NULL);
    // sc_switch_screen(s4_init, s4_update, NULL);
    while(1) {
        waitForScanline(255);
        keyb_update();
        sc_update();
    }
}