#include <arch/zxn.h>
#include <string.h>
#include <input.h>
static const char cmd_menu[] = "run menu.nex";
void esx_run_menu(void){ __asm ld ix,_cmd_menu
 rst 8
 defb 0x8f
__endasm; }
void main(void){
    memset((void*)0x4000,0x00,6144);
    memset((void*)0x5800,0x08,768);     /* blue paper = BLUE */
    zx_border(1);
    for(;;){ int k=in_inkey(); if(k=='m'||k=='M'){ esx_run_menu(); for(;;);} }
}
