#include "scr_main.h"

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include "screen_list.h"

void sm_init(void)
{
    tilemap_set_40col();
    // tilemap_set_80col();

    tilemap_clear_buf_value(' ');
    // tilemap_clear_buf_value('R');
    // tilemap_clear_buf_value(0x0252);

    print_set_color(0);

    print_set_pos(0, 1);
    println_ctr("nexlib library - \x03 2025 David Programa", 40);

    print_set_pos(0, 3);
    println_ctr("tilemapper example", 40);

    print_set_pos(0, 5);
    print_str("----------------------------------------");

    print_set_pos(0, 7);
    println_ctr("press numeric key to enter screen", 40);

    print_set_pos(2, 10);
    print_str("0) Attr ops - mirror X/Y, rotation");

    print_set_pos(2, 12);
    print_str("1) Tilemap generated with tool");

    // print_set_pos(2, 14);
    // print_str("2) Tilemap 80x32 with attributes");
}

void sm_update(void)
{
    if (keyb_is_just_pressed(KEY_ZX_0))
        sc_switch_screen(s0_init, s0_update, NULL);
    if (keyb_is_just_pressed(KEY_ZX_1))
        sc_switch_screen(s1_init, s1_update, NULL);
    // if (keyb_is_just_pressed(KEY_ZX_2))
    //     sc_switch_screen(s2_init, s2_update, NULL);
}

