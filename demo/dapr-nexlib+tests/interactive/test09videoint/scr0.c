#include "scr0.h"

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>
#include <util_next.h>
#include <interrupt.h>

extern u16 first_sp;
extern void main(void);

void s0_init(void)
{
    tilemap_clear_buf_value(' ');

    u16 sz = 40*26;
    u8* tm = (u8*)0x4000;
    u8 tile = 0;
    while (sz--) {
        *tm++ = tile;
        if (tile & 0x80) *tm++ = 0x10;
        else             *tm++ = 0;
        tile++;
    }

    print_set_pos(0, 26);
    print_str("SP=");
    print_hex_word(first_sp);
    print_str("~");
    print_hex_word(get_sp());
    print_str(" MAIN=");
    print_hex_word((void*)main);
    print_str(" ISR=");
    print_hex_word(interrupt_isr_vector);
    print_str(" SCAN=");
    print_dec_byte(interrupt_get_scanline());

}
static u16 hoffset = 0;
static u8 voffset = 0;

void s0_update(void)
{
    // tilemap_set_hoffset(hoffset);
    // tilemap_set_voffset(voffset);

    // tilemap_set_voffset(24);

    // hoffset++;
    // if (hoffset > 320) hoffset = 0;
    // voffset++;

    // if (keyb_is_just_pressed_any()) {
    //     sc_switch_screen(sm_init, sm_update, NULL);
    //     tilemap_set_hoffset(0);
    //     tilemap_set_voffset(0);
    // }

    ///////////////////////////////////////////////////////

    // tilemap_set_hoffset(0);
    // tilemap_set_voffset(0);
    // // tilemap_set_voffset(24);


    tilemap_set_hoffset(hoffset);
    tilemap_set_voffset(voffset);

    hoffset++;
    if (hoffset > 320) hoffset = 0;
    voffset++;

    // waitForScanline(207);

    // tilemap_set_hoffset(0);
    // tilemap_set_voffset(0);
}

void s0_interrupt(void)
{
    tilemap_set_hoffset(0);
    tilemap_set_voffset(0);
    // tilemap_set_voffset(24);


    // waitForScanline(50);

    // tilemap_set_hoffset(hoffset);
    // tilemap_set_voffset(voffset);

    // hoffset++;
    // if (hoffset > 320) hoffset = 0;
    // voffset++;
}