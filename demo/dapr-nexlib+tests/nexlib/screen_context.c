#include <arch/zxn.h>
#include <screen_context.h>
#include "zx0_decompress.h"
#include "tilemap_manager.h"
#include "palette_manager.h"

extern u8 ts_palgrp[];

void sc_load_context(ScrCtx* ctx)
{
    if (ctx->layer2_0A.page != 0)
    {
        // we have layer2
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_0A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_0A);
        zx0_decompress(ctx->layer2_0A.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_0B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_0B);
        zx0_decompress(ctx->layer2_0B.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_1A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_1A);
        zx0_decompress(ctx->layer2_1A.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_1B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_1B);
        zx0_decompress(ctx->layer2_1B.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_2A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_2A);
        zx0_decompress(ctx->layer2_2A.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_2B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_2B);
        zx0_decompress(ctx->layer2_2B.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_3A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_3A);
        zx0_decompress(ctx->layer2_3A.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_3B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_3B);
        zx0_decompress(ctx->layer2_3B.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_4A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_4A);
        zx0_decompress(ctx->layer2_4A.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_4B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LAYER2_4B);
        zx0_decompress(ctx->layer2_4B.cmp_data, DSTADDR_DECOMP);
        
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_pal.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_PALETTES);
        zx0_decompress(ctx->layer2_pal.cmp_data, DSTADDR_DECOMP);
        palette_load_layer2(DSTADDR_DECOMP);
    }

    if (ctx->spriteset_A.page != 0)
    {
        // we have spriteset
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->spriteset_A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_SPRITES_A);
        zx0_decompress(ctx->spriteset_A.cmp_data, DSTADDR_DECOMP);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->spriteset_B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_SPRITES_B);
        zx0_decompress(ctx->spriteset_B.cmp_data, DSTADDR_DECOMP);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->spriteset_pal.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_PALETTES);
        zx0_decompress(ctx->spriteset_pal.cmp_data, DSTADDR_DECOMP);
        palette_load_sprites(DSTADDR_DECOMP);
    }

    if (ctx->tileset_main.page != 0)
    {
        // we have tileset_main
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tileset_main.page);
        ZXN_NEXTREG(0x52, 10);
        ZXN_NEXTREG(0x53, 11);
        zx0_decompress(ctx->tileset_main.cmp_data, DSTADDR_TILESET_MAIN);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tileset_main_pal.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_PALETTES);
        zx0_decompress(ctx->tileset_main_pal.cmp_data, DSTADDR_DECOMP);
        palette_load_tilemap(DSTADDR_DECOMP);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tileset_main_palgrp.page);
        zx0_decompress(ctx->tileset_main_palgrp.cmp_data, ts_palgrp);

        tilemap_setup();
    }
    else {
        tilemap_disable();
    }

    if (ctx->tileset_text.page != 0)
    {
        // we have tileset_text
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tileset_text.page);
        ZXN_NEXTREG(0x52, 10);
        ZXN_NEXTREG(0x53, 11);
        zx0_decompress(ctx->tileset_text.cmp_data, DSTADDR_TILESET_TEXT);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tileset_text_pal.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_PALETTES);
        zx0_decompress(ctx->tileset_text_pal.cmp_data, DSTADDR_DECOMP);
        palette_load_tilemap_second(DSTADDR_DECOMP);
    }

    if (ctx->tilemap_A.page != 0)
    {
        // we have tilemap
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tilemap_A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_TILEMAP_A);
        zx0_decompress(ctx->tilemap_A.cmp_data, DSTADDR_DECOMP);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->tilemap_B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_TILEMAP_B);
        zx0_decompress(ctx->tilemap_B.cmp_data, DSTADDR_DECOMP);
    }

    if (ctx->collmap_A.page != 0)
    {
        // we have collmap
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->collmap_A.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_COLLMAP_A);
        zx0_decompress(ctx->collmap_A.cmp_data, DSTADDR_DECOMP);

        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->collmap_B.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_COLLMAP_B);
        zx0_decompress(ctx->collmap_B.cmp_data, DSTADDR_DECOMP);
    }

    if (ctx->levdata.page != 0)
    {
        ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->levdata.page);
        ZXN_NEXTREG(DSTSLOT_DECOMP_REG, DSTPAGE_LEVDATA);
        zx0_decompress(ctx->levdata.cmp_data, DSTADDR_DECOMP);
    }

    // zx0_decompress(gamefont_ts, (void*)DSTADDR_TILESET_TEXT);
    // // print_char('.');
    // zx0_decompress(jungle0_ts, (void*)DSTADDR_TILESET_MAIN);
    // // print_char('.');


    ZXN_NEXTREGA(SRCSLOT_DECOMP_REG, ctx->layer2_pal.page);
}

