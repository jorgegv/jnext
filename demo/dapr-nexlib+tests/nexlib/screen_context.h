#ifndef _screen_context_h_
#define _screen_context_h_

#include <types.h>

typedef struct BLOCKRES
{
    u8 page;
    u8* cmp_data;
}
BlockRes;

typedef struct PALRES
{
    u8 page;
    u8* cmp_data;
    u8 count;
    bool has_fade;
}
PalRes;

typedef struct CODERES
{
    u8 page;
    FunPtr init;
    FunPtr update;
    FunPtr exit;
}
CodeRes;

typedef struct SCRCTX
{
    BlockRes layer2_0A;
    BlockRes layer2_0B;
    BlockRes layer2_1A;
    BlockRes layer2_1B;
    BlockRes layer2_2A;
    BlockRes layer2_2B;
    BlockRes layer2_3A;
    BlockRes layer2_3B;
    BlockRes layer2_4A;
    BlockRes layer2_4B;

    PalRes layer2_pal;

    BlockRes tileset_main;
    PalRes tileset_main_pal;
    BlockRes tileset_main_palgrp;

    BlockRes tileset_text;
    PalRes tileset_text_pal;

    BlockRes spriteset_A;
    BlockRes spriteset_B;

    PalRes spriteset_pal;

    BlockRes tilemap_A;
    BlockRes tilemap_B;
    BlockRes collmap_A;
    BlockRes collmap_B;

    BlockRes levdata;

    // CodeRes code;
}
ScrCtx;

// context size: 18*3 + 4*5 + 7 = 81

void sc_load_context(ScrCtx* ctx);

#endif