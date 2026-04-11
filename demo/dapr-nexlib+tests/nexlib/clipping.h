#ifndef clipping_h
#define clipping_h

#include "types.h"

// CLIPPING
// maximum viewing zone is 320x256
// example: centered clipping to 288, 192
// 320 = 16 + 288 + 16
// 256 = 32 + 192 + 32
//
//  +--16--+---------------288---------------+--16--+
//  |      |                                 |      |
// 32      |                                 |      |
//  |      |                                 |      |
//  +------+---------------------------------+------+
//  |      |                                 |      |
//  |      |                                 |      |
//  |      |                                 |      |
//  |      |             Visible             |      |
// 192     |              area               |      |
//  |      |                                 |      |
//  |      |                                 |      |
//  |      |                                 |      |
//  |      |                                 |      |
//  +------+---------------------------------+------+
//  |      |                                 |      |
// 32      |                                 |      |
//  |      |                                 |      |
//  +------+---------------------------------+------+

// set centered clipping for layer2, tilemap, sprites
void clip_ctr_all(u16 width, u16 height);

void clip_ctr_layer2 (u16 width, u16 height);
void clip_ctr_sprites(u16 width, u16 height);
void clip_ctr_tilemap(u16 width, u16 height);
void clip_ula(u8 x1, u8 x2, u8 y1, u8 y2);


#endif