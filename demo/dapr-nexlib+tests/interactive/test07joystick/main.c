#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"
#include "palette_manager.h"
#include "palette_data.h"
#include <util_next.h>
#include "tilemap_manager.h"
#include <print_tile.h>
#include <keyb.h>
#include <joystick.h>

#include <string.h>

#include <util_next.h>
#include <string_util.h>

static void draw_joystick(void);
 
void main(void)
{
    set_cpu_speed_28();
    disable_contention();

    clip_ula(255, 255, 255, 255);

    clip_ctr_layer2(320, 256);

    layer2_init();

    // 000 010 011 -> 0000 1001 1 -> 0000 1001 0000 0001
    layer2_set_first_palette_entry(0x0901);
    layer2_clear_pages();

    // sprite layers and system register $15c
    // https://wiki.specnext.dev/Sprite_and_Layers_System_Register
    // clip-over-border, SUL, sprites-over-border, sprites-visible
    ZXN_NEXTREG(0x15, 0x2B);

    ZXN_NEXTREG(0x4B, 0x0F);

    palette_load_tilemap(tileset_palette);
    tilemap_setup();
    tilemap_clear_buf_value(' ');
    clip_ctr_tilemap(320, 256);

    print_set_pos(0, 1);
    println_ctr("nexlib library - \x03 2025 David Programa", 40);

    print_set_pos(0, 3);
    println_ctr("kempston joystick test", 40);

    print_set_pos(0, 5);
    print_str("----------------------------------------");

    joystick_init();

    static u8 ctr = 0;

    while(1) {
        waitForScanline(255);
        waitForScanline(255);
        waitForScanline(255);
        waitForScanline(255);

        joystick_update();
        joystick_update_flags();
        draw_joystick();
    }
}

static u8 x, y;
static u8 x0 = 7;
static u8 y0 = 10;
static u8 dx = 2;
static u8 dy = 3;

static u8 row0[] = {
    JOY_LEFT,
    JOY_UP,
    JOY_RIGHT,
    JOY_DOWN,
    JOY_BUT1,
    JOY_BUT2,
    JOY_BUT3,
    JOY_BUT4,
    0
};

void draw_joystick(void)
{
    print_set_color(0);
    print_set_pos(4, 8);
    print_str("joydata: ");
    print_str(str_hex_for_u8(joydata));
    print_char(' ');
    print_str(str_bin_for_u8(joydata));

    print_set_color(0);
    print_set_pos(4, 10);
    print_str("joyprev: ");
    print_str(str_hex_for_u8(joyprev));
    print_char(' ');
    print_str(str_bin_for_u8(joyprev));

    u8* prow0;
    u8 jcode0;

    print_set_color(0);
    print_set_pos(9, 13);
    print_str("is_pressed(JOY_*): ");
    prow0 = row0;
    while (0 != (jcode0 = *prow0++)) {
        u8 jsh = joystick_short_for_code(jcode0);
        if (joystick_is_pressed(jcode0))
            print_set_color(2);
        else
            print_set_color(0);
        u8 jch = joystick_ch4short[jsh];
        print_char(jch);
    }

    print_set_color(0);
    print_set_pos(4, 15);
    print_str("is_just_pressed(JOY_*): ");
    prow0 = row0;
    while (0 != (jcode0 = *prow0++)) {
        u8 jsh = joystick_short_for_code(jcode0);
        if (joystick_is_just_pressed(jcode0))
            print_set_color(2);
        else
            print_set_color(0);
        u8 jch = joystick_ch4short[jsh];
        print_char(jch);
    }

    print_set_color(0);
    print_set_pos(3, 17);
    print_str("is_just_released(JOY_*): ");
    prow0 = row0;
    while (0 != (jcode0 = *prow0++)) {
        u8 jsh = joystick_short_for_code(jcode0);
        if (joystick_is_just_released(jcode0))
            print_set_color(2);
        else
            print_set_color(0);
        u8 jch = joystick_ch4short[jsh];
        print_char(jch);
    }

    print_set_color(0);
    print_set_pos(5, 21);
    print_str("flags: ");

    print_set_color(joyLeft  ? 2 : 0); print_char('L');
    print_set_color(joyUp    ? 2 : 0); print_char('U');
    print_set_color(joyRight ? 2 : 0); print_char('R');
    print_set_color(joyDown  ? 2 : 0); print_char('D');
    print_set_color(joyBut1  ? 2 : 0); print_char('1');
    print_set_color(joyBut2  ? 2 : 0); print_char('2');
    print_set_color(joyBut3  ? 2 : 0); print_char('3');
    print_set_color(joyBut4  ? 2 : 0); print_char('4');

    print_set_color(0);
    print_set_pos(3, 27);
    print_str("code: ");
    print_hex_byte(joystick_code());
    print_str(" count: ");
    print_hex_byte(joystick_count());
    
    print_set_pos(3, 29);
    print_str("any is: ");
    print_hex_nibble(joystick_is_pressed_any());
    print_str(" was: ");
    print_hex_nibble(joystick_was_pressed_any());
    print_str("  jpre: ");
    print_hex_nibble(joystick_is_just_pressed_any());
    print_str("  jrel: ");
    print_hex_nibble(joystick_is_just_released_any());
}

