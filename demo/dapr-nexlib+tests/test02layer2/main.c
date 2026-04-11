#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"
#include "palette_manager.h"
#include "palette_data.h"

#include <string.h>

void main(void)
{
    // clip_ula (10, 20, 10, 20);

    clip_ctr_layer2(320, 256);

    layer2_init();

    layer2_set_ram_page(9);

    palette_load_layer2(kanagawa_palette);

    while(1);
}