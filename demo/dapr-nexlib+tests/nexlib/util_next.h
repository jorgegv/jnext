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

#ifndef __util_next__
#define __util_next__

#include "types.h"

extern void set_cpu_speed_28(void);
extern void disable_contention(void);
extern void enable_dacs(void);

extern void switchSlotPage8k(u8 slot, u8 page);

extern void selectTilemapPaletteFirst(void);
extern void selectTilemapPaletteSecond(void);

extern void disableULA(void);
extern void disable_8x5_entries_for_extended_keys(void);

extern u16 getActiveVideoLineWord(void);
extern u16 getLinesSinceBottomSync(void);

void waitForScanline(u16 targetLine);

// old version
// #include <arch/zxn.h>
// #define WAIT_FOR_SCANLINE(line) { \
//     while (ZXN_READ_REG(REG_ACTIVE_VIDEO_LINE_L) == line); \
//     while (ZXN_READ_REG(REG_ACTIVE_VIDEO_LINE_L) != line); } 

// fill bitmap area ($4000 - $57FF) with given value
void fillBitmapArea(u16 value);

// fill attribute area ($5800 - $5BFF) with given value
void fillAttributeArea(u16 value);

// fill tilemap area ($4000 - $4FFF) with given value
void fillTilemapArea(u16 value);

// experimental
u16 get_sp(void);

void set_sp(u16 newval);

#endif