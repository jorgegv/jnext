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
#include "pcm_player.h"

void main(void)
{
    set_cpu_speed_28();
    disable_contention();
    enable_dacs();

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
    println_ctr("dac / covox / pcm audio test", 40);

    print_set_pos(0, 5);
    print_str("----------------------------------------");

    print_set_pos(0, 7);
    println_ctr("press numeric key to play audio sample", 40);

    print_set_pos(5, 10);
    print_str("1) JUMP");

    print_set_pos(5, 12);
    print_str("2) DEATH");

    keyb_init();
    joystick_init();

    static u8 ctr = 0;

    while(1) {
        waitForScanline(255);
        keyb_update();
        joystick_update();

        if (keyb_is_just_pressed(KEY_ZX_1))
            pcm_play(PCM_JUMP);

        if (keyb_is_just_pressed(KEY_ZX_2))
            pcm_play(PCM_DEATH);

        if (keyb_is_just_pressed(KEY_ZX_0))
        {
            ZXN_NEXTREG(0x52, 10);
            print_set_pos(5, 29);
            print_str("stop ");
            static u8 ctr = 0;
            print_str(str_hex_for_u8(ctr++));
        }

    }
}

