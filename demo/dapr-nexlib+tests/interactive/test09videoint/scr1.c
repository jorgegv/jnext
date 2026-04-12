#include "scr1.h"

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>

void s1_init(void)
{
    tilemap_set_40col();

    tilemap_clear_buf_value(' ');

    print_set_color(0);

    print_set_pos(0, 1);
    println_ctr("tilemap 40 x 32 with attributes", 40);

    print_set_pos(0, 3);
    print_str("----------------------------------------");

    print_set_pos(0, 30);
    println_ctr("press any key to go back", 40);

    u8 x0 = 0;
    u8 y0 = 5;

    u8 x, y;
    u8 num = 0;

    y = y0;
    for (u8 j = 0; j < 12; j++) {
        y = y0 + 2 * j;
        for (u8 i = 0; i < 10; i++) {
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

void s1_update(void)
{
    if (keyb_is_just_pressed_any())
        sc_switch_screen(sm_init, sm_update, NULL);
}

