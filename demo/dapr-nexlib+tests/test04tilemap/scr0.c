#include "scr0.h"

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>

void s0_init(void)
{
    tilemap_clear_buf_value(' ');

    u16 sz = 40*32;
    u8* tm = (u8*)0x4000;
    u8 tile = 0;
    while (sz--) {
        *tm++ = tile;
        if (tile & 0x80) *tm++ = 0x10;
        else             *tm++ = 0;
        tile++;
    }
}
static u16 hoffset = 0;
static u8 voffset = 0;

void s0_update(void)
{
    tilemap_set_hoffset(hoffset);
    tilemap_set_voffset(voffset);

    hoffset++;
    if (hoffset > 320) hoffset = 0;
    voffset++;

    if (keyb_is_just_pressed_any()) {
        sc_switch_screen(sm_init, sm_update, NULL);
        tilemap_set_hoffset(0);
        tilemap_set_voffset(0);
    }
}

