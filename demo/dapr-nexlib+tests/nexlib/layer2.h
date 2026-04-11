#ifndef layer2_h
#define layer2_h

#include "types.h"

// initialize layer 2 with nexlib defaults. calls made:
// system_set_global_transparency_0();
// layer2_set_resolution(L2RES_320x256x8);
// layer2_set_ram_page(9);
// layer2_set_visible();
void layer2_init(void);

// resolutions for layer2_set_resolution
#define L2RES_256x192x8 0x00 // 0b00000000
#define L2RES_320x256x8 0x10 // 0b00010000
#define L2RES_640x256x4 0x20 // 0b00100000

void layer2_set_resolution(u8 res) fastcall;
void layer2_set_visible(void);
void layer2_set_invisible(void);
void layer2_set_ram_page(u8 num_16k_bank) fastcall;

// set first palette entry for layer 2.
// palentry bit pattern: MSB RRRGGGBB 0000000B LSB
void layer2_set_first_palette_entry(u16 palentry) fastcall;

// clear all layer2 pages
void layer2_clear_pages(void);

void layer2_set_hoffset(u16 hoff) fastcall;

#endif
