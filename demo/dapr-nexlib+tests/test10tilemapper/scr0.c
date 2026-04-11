#include "scr0.h"

#include <print_tile.h>
#include <tilemap_manager.h>
#include <keyb.h>
#include <screen_list.h>

void s0_init(void)
{
    tilemap_clear_buf_value(' ');

    for (u8 i = 0; i < 8; i++) {
        print_set_pos(10, 8+2*i);
        print_set_attr(0);
        print_hex_byte(i << 1);
        print_char(' ');
        print_set_attr(i << 1);
        print_char('R');
        print_char(' ');
        print_set_attr(0);
        print_str(i & 1 ? "ROT " : "    ");
        print_str(i & 2 ? "MIY " : "    ");
        print_str(i & 4 ? "MIX " : "    ");
    }
}

// // attributes for mirror X, mirror Y, Rotation
// #define ATTR_____ 0
// #define ATTR___R_ 2
// #define ATTR__Y__ 4
// #define ATTR__YR_ 6
// #define ATTR_X___ 8
// #define ATTR_X_R_ 10
// #define ATTR_XY__ 12
// #define ATTR_XYR_ 14


void s0_update(void)
{
    if (keyb_is_just_pressed_any()) {
        sc_switch_screen(sm_init, sm_update, NULL);
    }
}

