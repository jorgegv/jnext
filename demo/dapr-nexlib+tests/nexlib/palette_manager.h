///////////////////////////////////////////////////////////////////////////////
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
//
///////////////////////////////////////////////////////////////////////////////

#ifndef _palette_manager_h_
#define _palette_manager_h_

#include "types.h"

void palette_load_tilemap(u8* pal_data);
void palette_load_tilemap_second(u8* pal_data);
void palette_load_sprites(u8* pal_data);
void palette_load_layer2(u8* pal_data);

void palette_load_layer2_16col(u8* pal_data);

// #define PALETTE_TILEMAP 48
// #define PALETTE_SPRITES 32
// #define PALETTE_LAYER_2 16

// void palette_load(u8 dest_pal, u8* pal_data);
// void palette_load(u8* pal_data);

// bits 6-4 of 7-0
// %000 	ULA first palette
// %100 	ULA second palette
// %001 	Layer 2 first palette
// %101 	Layer 2 second palette
// %010 	Sprites first palette
// %110 	Sprites second palette
// %011 	Tilemap first palette
// %111 	Tilemap second palette 

#endif