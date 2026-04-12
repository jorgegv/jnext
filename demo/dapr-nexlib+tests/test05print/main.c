#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"
#include "palette_manager.h"
#include "palette_data.h"
#include <util_next.h>
#include "tilemap_manager.h"
#include <print_tile.h>

#include <string.h>

#include <util_next.h>

#include <string_util.h>
 
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

    print_set_pos(0, 0);
    println_ctr("nexlib library - \x03 2025 David Programa", 40);

    print_set_pos(0, 1);
    println_ctr("text functions test", 40);

    print_set_pos(0, 2);
    print_str("----------------------------------------");

    // print_set_attr(4);
    // print_set_color(2);

    // Use fixed test values so output is deterministic for screenshot testing.
    // Each value exercises different bit patterns across the print functions.
    static const u16 test_values[] = { 0x94, 0x4A, 0x25, 0x12, 0x09, 0x04, 0x02, 0x01 };

    while(1) {
        waitForScanline(255);
        u16 u16val;

        u8 y = 3;

        print_set_pos(0, y++);
        print_str("chr_hex_for_u4 : ");

        for (u8 i = 0; i < 23; i++)
            print_char(chr_hex_for_u4(i));

        print_set_pos(0, y++);
        print_str("print_hex_nible: ");

        for (u8 i = 0; i < 23; i++)
            print_hex_nibble(i);

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("str_hex_for_u8 : ");
        for (u8 i = 0; i < 8; i++) {
            if (i) print_char(' ');
            print_str(str_hex_for_u8(test_values[i]));
        }

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("print_hex_byte : ");
        for (u8 i = 0; i < 8; i++) {
            if (i) print_char(' ');
            print_hex_byte(test_values[i]);
        }

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("str_hex_for_u16: ");
        u16val = 0x0094;
        print_str(str_hex_for_u16(u16val));
        print_str("  ");
        u16val = 0x004A;
        print_str(str_hex_for_u16(u16val));
        print_str("  ");
        u16val = 0x0025;
        print_str(str_hex_for_u16(u16val));
        print_str("  ");
        u16val = 0x0012;
        print_str(str_hex_for_u16(u16val));

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("print_hex_word : ");
        print_hex_word(0x0094);
        print_str("  ");
        print_hex_word(0x004A);
        print_str("  ");
        print_hex_word(0x0025);
        print_str("  ");
        print_hex_word(0x0012);

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("str_bin_for_u8 : ");
        print_str(str_bin_for_u8(0x94));
        print_char(' ');
        print_str(str_bin_for_u8(0x04));

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("str_bin_for_u16: ");
        print_str(str_bin_for_u16(0x0094));

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("print_dec_byte : ");
        print_dec_byte(0);
        print_char(' ');
        print_dec_byte(5);
        print_char(' ');
        print_dec_byte(8);
        print_char(' ');
        print_dec_byte(10);
        print_char(' ');
        print_dec_byte(55);
        print_char(' ');
        print_dec_byte(100);
        print_char(' ');
        print_dec_byte(122);
        print_char(' ');
        print_dec_byte(214);

        ///////////////////////////////////////
        // print_set_pos(0, y++);
        // print_str("str_dec_for_u8 : ");
        // print_str(str_dec_for_u8(0));
        // print_char(' ');
        // print_str(str_dec_for_u8(5));
        // print_char(' ');
        // print_str(str_dec_for_u8(8));
        // print_char(' ');
        // print_str(str_dec_for_u8(10));
        // print_char(' ');
        // print_str(str_dec_for_u8(55));
        // print_char(' ');
        // print_str(str_dec_for_u8(100));
        // print_char(' ');
        // print_str(str_dec_for_u8(122));
        // print_char(' ');
        // print_str(str_dec_for_u8(214));

        ///////////////////////////////////////
        print_set_pos(0, y++);
        print_str("print_dec_word : ");
        print_dec_word(0);
        print_char(' ');
        print_dec_word(57);
        print_char(' ');
        print_dec_word(100);
        print_char(' ');
        print_dec_word(555);
        print_char(' ');
        print_dec_word(1000);
        print_char(' ');
        print_dec_word(40320);
 
 
    }
}