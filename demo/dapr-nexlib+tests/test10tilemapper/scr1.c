#include "scr1.h"

#include <arch/zxn.h>

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>
#include <palette_manager.h>
#include "palette_data.h"

#include <string.h>

void s1_init(void)
{
    tilemap_set_40col();

    palette_load_tilemap(screen01_palette);

    // copy tileset
    ZXN_NEXTREG(0x52, 41);
    ZXN_NEXTREG(0x53, 11);
    memcpy(0x6000, 0x4000, 0x2000);

    // copy tilemap
    ZXN_NEXTREG(0x52, 42);
    ZXN_NEXTREG(0x53, 10);
    memcpy(0x6000, 0x4000, 0x2000);

    ZXN_NEXTREG(0x52, 10);
    ZXN_NEXTREG(0x53, 11);

}

void s1_update(void)
{
    if (keyb_is_just_pressed_any())
    {
        palette_load_tilemap(tileset_palette);

        // copy text tileset
        ZXN_NEXTREG(0x52, 40);
        ZXN_NEXTREG(0x53, 11);
        memcpy(0x6000, 0x4000, 0x2000);
        ZXN_NEXTREG(0x52, 10);
        ZXN_NEXTREG(0x53, 11);

        sc_switch_screen(sm_init, sm_update, NULL);
    }
}

