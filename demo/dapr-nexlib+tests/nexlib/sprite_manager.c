///////////////////////////////////////////////////////////////////////////////
//
//  Celeste Classic (remake) - for ZX Spectrum Next / N-Go
//
//  (c) 2024 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
//  Based on Celeste Classic for Pico-8 - (c) 2015 Maddy Thorson, Noel Berry
//
///////////////////////////////////////////////////////////////////////////////
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.  
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
// 
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>. 
//
///////////////////////////////////////////////////////////////////////////////

#include "sprite_manager.h"

#include <arch/zxn.h>

// attributes for mirror X, mirror Y, Rotation
#define ATTR_____ 0
#define ATTR___R_ 2
#define ATTR__Y__ 4
#define ATTR__YR_ 6
#define ATTR_X___ 8
#define ATTR_X_R_ 10
#define ATTR_XY__ 12
#define ATTR_XYR_ 14

s16 sprite_global_offset_x = 0;
s16 sprite_global_offset_y = 0;

void sprite_update_c(SpriteDef* sprdef)
{
    u16 sx = sprdef->x + sprite_global_offset_x;
    u16 sy = sprdef->y + sprite_global_offset_y;
    ZXN_NEXTREGA(0x34, sprdef->slot);
    ZXN_NEXTREGA(0x35, sx);
    ZXN_NEXTREGA(0x36, sy);
    u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
    val37 |= (sx >> 8) & 1;
    ZXN_NEXTREGA(0x37, val37);
    ZXN_NEXTREGA(0x38, 0xC0 | (sprdef->pat >> 1));
    u8 val39 = 0xA0 | ((sprdef->pat & 1) << 6);
    val39 |= (sy >> 8) & 1;
    val39 |= sprdef->scale;
    ZXN_NEXTREGA(0x39, val39);
}

// void sprite_update_pos(SpriteDef* sprdef)
// {

// }

void sprite_init_quad(SpriteDef* sprdef)
{
    // changes in init_quad:
    // - initial coordinates 0,0
    // - anchor sprite initially invisible
    // - no mirrot in subordinate sprites

    u8 slot = sprdef->slot;
    u8 pat = sprdef->pat;

    // anchor sprite will be inited by sprite_init_quad

    // u16 sx = 0;
    // u16 sy = 0;
    // ZXN_NEXTREGA(0x34, slot);
    // ZXN_NEXTREGA(0x35, sx);     // x
    // ZXN_NEXTREGA(0x36, sy);
    // u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
    // val37 |= (sx >> 8) & 1;
    // ZXN_NEXTREGA(0x37, val37);
    // ZXN_NEXTREGA(0x38, 0x60 | (pat >> 1));
    // u8 val39 = 0xA0 | ((pat & 1) << 6);
    // val39 |= (sy >> 8) & 1;
    // val39 |= sprdef->scale;
    // ZXN_NEXTREGA(0x39, val39);

    #define RPAT1 1
    #define RPAT2 2
    #define RPAT3 3

    slot++;

    ZXN_NEXTREGA(0x34, slot);
    ZXN_NEXTREG (0x35, 0x10);   // x-offset = 16
    ZXN_NEXTREG (0x36, 0x00);   // y-offset = 0
    ZXN_NEXTREG (0x37, 0x01);   // pal_offset 0, no mirrot, relative palette offset
    ZXN_NEXTREG (0x38, 0xC0);   // visible, 5th byte on, pattern msbs (0)
    ZXN_NEXTREG (0x39, 0x61);   // relative, pat lsb(1), relative pattern offset

    slot++;

    ZXN_NEXTREGA(0x34, slot);
    ZXN_NEXTREG (0x35, 0x00);   // x-offset = 0
    ZXN_NEXTREG (0x36, 0x10);   // y-offset = 16
    ZXN_NEXTREG (0x37, 0x01);   // pal_offset 0, no mirrot, relative palette offset
    ZXN_NEXTREG (0x38, 0xC1);   // visible, 5th byte on, pattern msbs (1)
    ZXN_NEXTREG (0x39, 0x41);   // relative, pat lsb(0), relative pattern offset

    slot++;

    ZXN_NEXTREGA(0x34, slot);
    ZXN_NEXTREG (0x35, 0x10);   // x-offset = 16
    ZXN_NEXTREG (0x36, 0x10);   // y-offset = 16
    ZXN_NEXTREG (0x37, 0x01);   // pal_offset 0, no mirrot, relative palette offset
    ZXN_NEXTREG (0x38, 0xC1);   // visible, 5th byte on, pattern msbs (1)
    ZXN_NEXTREG (0x39, 0x61);   // relative, pat lsb(1), relative pattern offset
}

void sprite_update_quad(SpriteDef* sprdef)
{
    u16 sx = sprdef->x;
    if (sprdef->mirrot & ATTR_X___) sx += 16;
    u16 sy = sprdef->y;
    ZXN_NEXTREGA(0x34, sprdef->slot);
    ZXN_NEXTREGA(0x35, sx);
    ZXN_NEXTREGA(0x36, sy);
    u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
    val37 |= (sx >> 8) & 1;
    ZXN_NEXTREGA(0x37, val37);

    // visible, 
    ZXN_NEXTREGA(0x38, 0xC0 | (sprdef->pat >> 1));

    // anchor for big sprite, pat lsb, scale, y msb
    u8 val39 = 0xA0 | ((sprdef->pat & 1) << 6);
    val39 |= (sy >> 8) & 1;
    val39 |= sprdef->scale;
    ZXN_NEXTREGA(0x39, val39);
}


void sprite_init_hpair(SpriteDef* sprdef)
{
    // changes in init_hpair:
    // - initial coordinates 0,0
    // - anchor sprite initially invisible
    // - no mirrot in subordinate sprites

    u8 slot = sprdef->slot;
    u8 pat = sprdef->pat;

    // anchor sprite will be inited by sprite_init_hpair
    
    // u16 sx = 0;
    // u16 sy = 0;
    // ZXN_NEXTREGA(0x34, slot);
    // ZXN_NEXTREGA(0x35, sx);     // x
    // ZXN_NEXTREGA(0x36, sy);
    // u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
    // val37 |= (sx >> 8) & 1;
    // ZXN_NEXTREGA(0x37, val37);
    // ZXN_NEXTREGA(0x38, 0x60 | (pat >> 1));
    // u8 val39 = 0xA0 | ((pat & 1) << 6);
    // val39 |= (sy >> 8) & 1;
    // val39 |= sprdef->scale;
    // ZXN_NEXTREGA(0x39, val39);

    #define RPAT1 1
    #define RPAT2 2
    #define RPAT3 3

    slot++;

    ZXN_NEXTREGA(0x34, slot);
    ZXN_NEXTREG (0x35, 0x10);   // x-offset = 16
    ZXN_NEXTREG (0x36, 0x00);   // y-offset = 0
    ZXN_NEXTREG (0x37, 0x01);   // pal_offset 0, no mirrot, relative palette offset
    ZXN_NEXTREG (0x38, 0xC0);   // visible, 5th byte on, pattern msbs (0)
    ZXN_NEXTREG (0x39, 0x61);   // relative, pat lsb(1), relative pattern offset

    slot++;

    ZXN_NEXTREGA(0x34, slot);
    ZXN_NEXTREG (0x35, 0x00);   // x-offset = 0
    ZXN_NEXTREG (0x36, 0x10);   // y-offset = 16
    ZXN_NEXTREG (0x37, 0x01);   // pal_offset 0, no mirrot, relative palette offset
    ZXN_NEXTREG (0x38, 0xC1);   // visible, 5th byte on, pattern msbs (1)
    ZXN_NEXTREG (0x39, 0x41);   // relative, pat lsb(0), relative pattern offset

    slot++;

    ZXN_NEXTREGA(0x34, slot);
    ZXN_NEXTREG (0x35, 0x10);   // x-offset = 16
    ZXN_NEXTREG (0x36, 0x10);   // y-offset = 16
    ZXN_NEXTREG (0x37, 0x01);   // pal_offset 0, no mirrot, relative palette offset
    ZXN_NEXTREG (0x38, 0xC1);   // visible, 5th byte on, pattern msbs (1)
    ZXN_NEXTREG (0x39, 0x61);   // relative, pat lsb(1), relative pattern offset
}

void sprite_update_hpair(SpriteDef* sprdef)
{
    u16 sx = sprdef->x;
    if (sprdef->mirrot & ATTR_X___) sx += 16;
    u16 sy = sprdef->y;
    ZXN_NEXTREGA(0x34, sprdef->slot);
    ZXN_NEXTREGA(0x35, sx);
    ZXN_NEXTREGA(0x36, sy);
    u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
    val37 |= (sx >> 8) & 1;
    ZXN_NEXTREGA(0x37, val37);

    // visible, 
    ZXN_NEXTREGA(0x38, 0xC0 | (sprdef->pat >> 1));

    // anchor for big sprite, pat lsb, scale, y msb
    u8 val39 = 0xA0 | ((sprdef->pat & 1) << 6);
    val39 |= (sy >> 8) & 1;
    val39 |= sprdef->scale;
    ZXN_NEXTREGA(0x39, val39);
}



// extern u16 shake_spr_x;
// extern u16 shake_spr_y;

// void sprite_update_c(SpriteDef* sprdef)
// {
//     u16 sx = sprdef->x + shake_spr_x;
//     u16 sy = sprdef->y + shake_spr_y;
//     ZXN_NEXTREGA(0x34, sprdef->slot);
//     ZXN_NEXTREGA(0x35, sx);
//     ZXN_NEXTREGA(0x36, sy);
//     u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
//     val37 |= (sx >> 8) & 1;
//     ZXN_NEXTREGA(0x37, val37);
//     ZXN_NEXTREGA(0x38, 0xC0 | (sprdef->pat >> 1));
//     u8 val39 = 0x80 | ((sprdef->pat & 1) << 6);
//     val39 |= (sy >> 8) & 1;
//     val39 |= sprdef->scale;
//     ZXN_NEXTREGA(0x39, val39);
// }


// extern SpriteAttr sprite_list[];

// void sprite_set_x(u8 sidx, u16 x) {
//     SpriteAttr* sa = &sprite_list[sidx];
//     sa->status |= SS_MUST_UPDATE;
//     sa->att0 = x;
//     sa->att2 &= 0xFE;
//     sa->att2 |= ((x >> 8) & 1);
// }

// void sprite_set_y(u8 sidx, u16 y) {

// }

// void sprite_set_pal(u8 sidx, u8 pal) {

// }

// void sprite_set_mirrot(u8 sidx, u8 mirrot) {

// }

// void sprite_set_pat(u8 sidx, u8 pat) {

// }

// void sprite_set_scl(u8 sidx, u8 sclxy) {

// }

