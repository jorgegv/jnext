#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"
#include "palette_manager.h"
#include "palette_data.h"
#include <util_next.h>
#include "tilemap_manager.h"
#include <print_tile.h>
#include <keyb.h>

#include <string.h>
#include <string_util.h>
#include <util_next.h>

static void draw_keyboard(void);
 
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
    println_ctr("keyboard test", 40);

    print_set_pos(0, 5);
    print_str("----------------------------------------");

    print_set_pos(0, 7);
    println_ctr("is pressed", 40);

    print_set_pos(0, 20);
    println_ctr(" is just pressed   is just released", 40);


    keyb_init();

    while(1) {
        waitForScanline(255);
        waitForScanline(255);
        waitForScanline(255);
        waitForScanline(255);

        keyb_update();
        draw_keyboard();
    }
}

u16 row0[] = {
    KEY_NX_BREAK,
    KEY_NX_EDIT,
    KEY_ZX_1,
    KEY_ZX_2,
    KEY_ZX_3,
    KEY_ZX_4,
    KEY_ZX_5,
    KEY_ZX_6,
    KEY_ZX_7,
    KEY_ZX_8,
    KEY_ZX_9,
    KEY_ZX_0,
    KEY_NX_DEL,
    0
};

u16 row1[] = {
    KEY_NX_TRUVIDEO,
    KEY_NX_INVVIDEO,
    KEY_ZX_Q,
    KEY_ZX_W,
    KEY_ZX_E,
    KEY_ZX_R,
    KEY_ZX_T,
    KEY_ZX_Y,
    KEY_ZX_U,
    KEY_ZX_I,
    KEY_ZX_O,
    KEY_ZX_P,
    0    
};

u16 row2[] = {
    KEY_NX_CAPSLOCK,
    KEY_NX_GRAPH,
    KEY_ZX_A,
    KEY_ZX_S,
    KEY_ZX_D,
    KEY_ZX_F,
    KEY_ZX_G,
    KEY_ZX_H,
    KEY_ZX_J,
    KEY_ZX_K,
    KEY_ZX_L,
    KEY_ZX_ENT,
    0    
};

u16 row3[] = {
    KEY_ZX_CAP,
    KEY_NX_EXTEND,
    KEY_ZX_Z,
    KEY_ZX_X,
    KEY_ZX_C,
    KEY_ZX_V,
    KEY_ZX_B,
    KEY_ZX_N,
    KEY_ZX_M,
    KEY_NX_UP,
    KEY_ZX_CAP,
    0    
};

u16 row4[] = {
    KEY_ZX_SYM,
    KEY_NX_SEMICOLON,
    KEY_NX_QUOTE,
    KEY_NX_COMMA,
    KEY_NX_PERIOD,
    0xFFFF,
    KEY_ZX_SPC,
    0xFFFF,
    KEY_NX_LEFT,
    KEY_NX_DOWN,
    KEY_NX_RIGHT,
    KEY_ZX_SYM,
    0
};

static u8 x, y;
static u8 x0 = 7;
static u8 y0 = 9;
static u8 dx = 2;
static u8 dy = 2;

void draw_row(u16* row)
{
    u16 keycode;
    while (0 != (keycode = *row++))
    {
        if (keycode == 0xFFFF) { x += dx; continue; }

        print_set_pos(x, y);

        u8 keyshort = keyb_short_for_code(keycode);
        char keychar = keyb_ch4short[keyshort];
        if (keyb_is_pressed(keycode))
            print_set_color(2);
        else
            print_set_color(0);

        print_char(keychar);

        x += dx;
    }
}

void draw_row_p(u16* row)
{
    u16 keycode;
    while (0 != (keycode = *row++))
    {
        if (keycode == 0xFFFF) { x += dx; continue; }

        print_set_pos(x, y);

        u8 keyshort = keyb_short_for_code(keycode);
        char keychar = keyb_ch4short[keyshort];
        if (keyb_is_just_pressed(keycode))
            print_set_color(2);
        else
            print_set_color(0);

        print_char(keychar);

        x += dx;
    }
}

void draw_row_r(u16* row)
{
    u16 keycode;
    while (0 != (keycode = *row++))
    {
        if (keycode == 0xFFFF) { x += dx; continue; }

        print_set_pos(x, y);

        u8 keyshort = keyb_short_for_code(keycode);
        char keychar = keyb_ch4short[keyshort];
        if (keyb_is_just_released(keycode))
            print_set_color(2);
        else
            print_set_color(0);

        print_char(keychar);

        x += dx;
    }
}

void draw_keyboard(void)
{
    x0 = 7;
    y0 = 9;
    dx = 2;
    dy = 2;

    x = x0+0; y  = y0; draw_row(row0);
    x = x0+1; y += dy; draw_row(row1);
    x = x0+1; y += dy; draw_row(row2);
    x = x0+2; y += dy; draw_row(row3);
    x = x0+2; y += dy; draw_row(row4);

    x0 = 4;
    y0 = 22;
    dx = 1;
    dy = 1;

    x = x0+0; y  = y0; draw_row_p(row0);
    x = x0+0; y += dy; draw_row_p(row1);
    x = x0+0; y += dy; draw_row_p(row2);
    x = x0+0; y += dy; draw_row_p(row3);
    x = x0+0; y += dy; draw_row_p(row4);

    x0 = 23;
    y0 = 22;
    dx = 1;
    dy = 1;

    x = x0+0; y  = y0; draw_row_r(row0);
    x = x0+0; y += dy; draw_row_r(row1);
    x = x0+0; y += dy; draw_row_r(row2);
    x = x0+0; y += dy; draw_row_r(row3);
    x = x0+0; y += dy; draw_row_r(row4);

    print_set_pos(3, 28);
    print_str("code: ");
    print_hex_word(keyb_code());
    print_str(" count: ");
    print_hex_byte(keyb_count());
    
    print_set_pos(3, 29);
    print_str("any is: ");
    print_hex_nibble(keyb_is_pressed_any());
    print_str(" was: ");
    print_hex_nibble(keyb_was_pressed_any());
    print_str("  jpre: ");
    print_hex_nibble(keyb_is_just_pressed_any());
    print_str("  jrel: ");
    print_hex_nibble(keyb_is_just_released_any());

}
































    // __asm__("extern _print_hex_byte");
    // __asm__("extern _print_char");
    // __asm__("ld a, 0x7f");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");

    // __asm__("push af");
    // __asm__("ld l, a");
    // __asm__("call _print_hex_byte");
    // __asm__("ld l, 32");
    // __asm__("call _print_char");
    // __asm__("pop af");
    // __asm__("rrca");


