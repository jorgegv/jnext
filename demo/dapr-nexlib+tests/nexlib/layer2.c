///////////////////////////////////////////////////////////////////////////////
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#include "layer2.h"
#include <string.h>
#include <arch/zxn.h>
#include "types.h"

void system_set_global_transparency_0(void);
void tilemap_set_transparent_index_0(void);

void layer2_setup(void);

void layer2_set_dummy_pattern(void);


void layer2_init(void)
{
    system_set_global_transparency_0();

    layer2_set_resolution(L2RES_320x256x8);

    layer2_set_ram_page(9);

    layer2_set_visible();
}

void layer2_set_hoffset(u16 hoff) fastcall
{
    // https://wiki.specnext.dev/Layer_2_X_Offset_Register
    // https://wiki.specnext.dev/Layer_2_X_Offset_MSB_Register
    u8 ohi = (hoff >> 8) & 1;
    u8 olo = (hoff     ) & 0xFF;
    ZXN_NEXTREGA(0x71, ohi);
    ZXN_NEXTREGA(0x16, olo);
}
