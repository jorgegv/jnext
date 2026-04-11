#include <arch/zxn.h>

#include "layer2.h"
#include "clipping.h"

#include <string.h>

void main(void)
{
    // clip_ula (10, 20, 10, 20);

    clip_ctr_layer2(320, 256);

    layer2_init();

#define L2_RED 0x40 // 010 000 00
#define L2_YEL 0x48 // 010 010 00
#define L2_GRE 0x08 // 000 010 00
#define L2_CYA 0x09 // 000 010 01
#define L2_BLU 0x01 // 000 000 01
#define L2_MAG 0x41 // 010 000 01 


    // https://wiki.specnext.dev/Layer_2_RAM_Page_Register
    // set layer2 ram starting page to 16K page 9
    __asm__("NEXTREG 0x12, 9");

    __asm__("NEXTREG 0x50, 18");
    __asm__("NEXTREG 0x51, 19");
    memset((u8*)0x0000, L2_RED, (size_t)0x4000);

    __asm__("NEXTREG 0x50, 20");
    __asm__("NEXTREG 0x51, 21");
    memset((u8*)0x0000, L2_YEL, (size_t)0x4000);

    __asm__("NEXTREG 0x50, 22");
    __asm__("NEXTREG 0x51, 23");
    memset((u8*)0x0000, L2_GRE, (size_t)0x4000);

    __asm__("NEXTREG 0x50, 24");
    __asm__("NEXTREG 0x51, 25");
    memset((u8*)0x0000, L2_CYA, (size_t)0x4000);

    __asm__("NEXTREG 0x50, 26");
    __asm__("NEXTREG 0x51, 27");
    memset((u8*)0x0000, L2_BLU, (size_t)0x4000);

    while(1);
}