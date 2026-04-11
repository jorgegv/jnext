#include "scr2.h"

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>

void s2_init(void)
{
    tilemap_set_80col();

    tilemap_clear_buf_value(' ');

    print_set_color(0);

    print_set_pos(0, 1);
    println_ctr("tilemap 80 x 32 with attributes", 80);

    print_set_pos(0, 3);
    print_str("----------------------------------------");
    print_str("----------------------------------------");

    print_set_pos(0, 30);
    println_ctr("press any key to go back", 80);

    u8 x0 = 0;
    u8 y0 = 5;

    u8 x, y;
    u8 num = 0;

    for (u8 j = 0; j < 12; j++) {
        y = y0 + 2 * j;
        for (u8 i = 0; i < 20; i++) {
            x = x0 + 4 * i;
            print_set_pos(x, y);

            u8 color = ((i+j)&1) ? 0 : 2;
            print_set_color(color);

            print_char(' ');
            print_hex_byte(num++);
            print_char(' ');
        }
        y += 2;
    }
}

void s2_update(void)
{
    if (keyb_is_just_pressed_any())
        sc_switch_screen(sm_init, sm_update, NULL);
}

