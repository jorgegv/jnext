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

#ifndef _sprite_manager_h_
#define _sprite_manager_h_

#include "types.h"

void sprite_setup(void);
void sprite_load_defs(void);

void sprite_clear_all_slots(void);
void sprite_clear_all_from(u8 first_slot_to_clear);
void sprite_clear_slot(u8 slot);

typedef struct
{
    u8  slot;       // sprite slot
    s16 x;          // x coordinate of sprite
    s16 y;          // y coordinate of sprite
    u8  pal;         // palette offset
    u8  mirrot;      // combined mirror X / mirror Y / rotation
    u8  pat;         // index of current pattern
    u8  scale;      // scale of current sprite 
} SpriteDef;

void sprite_update_c(SpriteDef* spr);
void sprite_update_asm(SpriteDef* spr);

void sprite_init_quad(SpriteDef* sprdef);
void sprite_update_quad(SpriteDef* spr);

void sprite_init_hpair(SpriteDef* sprdef);
void sprite_update_hpair(SpriteDef* spr);

#define sprite_update sprite_update_c

void sprite_update_pos(SpriteDef* sprdef);

void sprite_text_print(SpriteDef* sdef, const char* msg, bool centerX);

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

// void sprite_set_x(u8 sidx, u16 x);
// void sprite_set_y(u8 sidx, u16 y);
// void sprite_set_pal(u8 sidx, u8 pal);
// void sprite_set_mirrot(u8 sidx, u8 mirrot);
// void sprite_set_pat(u8 sidx, u8 pat);
// void sprite_set_scl(u8 sidx, u8 sclxy);

// // sprite status
// #define SS_MUST_UPDATE 0x80

// typedef struct {
//     u8 status;
//     u8 att0;    // lsb X
//     u8 att1;    // lsb Y
//     u8 att2;    // pal, mirrot, msb X for anchor
//     u8 att3;    // vis, att4en, pat
//     u8 att4;    // anchor/relative, pat6, scale, msb Y for anchor
// } SpriteAttr;

// void sprite_draw_list();


#define SCALE_Y1 0x00
#define SCALE_Y2 0x02
#define SCALE_Y4 0x04
#define SCALE_Y8 0x06
#define SCALE_X1 0x00
#define SCALE_X2 0x08
#define SCALE_X4 0x10
#define SCALE_X8 0x18

#endif