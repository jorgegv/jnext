#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"
#include "palette_manager.h"
#include "palette_data.h"
#include "dma.h"
#include "sprite_manager.h"
#include <util_next.h>

#include <string.h>
 
SpriteDef sdef;

void main(void)
{
    clip_ctr_layer2(320, 256);

    layer2_init();

    // 000 010 011 -> 0000 1001 1 -> 0000 1001 0000 0001
    layer2_set_first_palette_entry(0x0901);
    layer2_clear_pages();

    palette_load_sprites(spriteset_palette);

    ZXN_NEXTREG(0x50, 30);
    ZXN_NEXTREG(0x51, 31);
    dma_transfer_sprite(0x0000, 0x4000);

    sprite_setup();

    sdef.slot = 0;
    sdef.pal = 0;
    sdef.pat = 0;
    sdef.mirrot = 0;
    sdef.scale = 0;
    sdef.x = 152;
    sdef.y = 120;

    sprite_update(&sdef);

    while(1) {
        waitForScanline(255);

        static u8 frame = 0;

        sdef.pat = (frame++ >> 2) & 0x0F;

        sprite_update(&sdef);
    }
}