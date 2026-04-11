///////////////////////////////////////////////////////////////////////////////
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#ifndef __tilemap_manager_h__
#define __tilemap_manager_h__

#include <types.h>

void tilemap_setup(void);
void tilemap_disable(void);

void tilemap_set_40col(void);
void tilemap_set_80col(void);

void tilemap_set_tilemap_seg(u8 tmseg) fastcall;
void tilemap_set_tileset_seg(u8 tsseg) fastcall;

void tilemap_clear_buf_value(u16 value) fastcall;

void tilemap_set_hoffset(s16 hoffset) fastcall;
void tilemap_set_voffset(s8  voffset) fastcall;

#endif
