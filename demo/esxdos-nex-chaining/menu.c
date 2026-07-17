#include <arch/zxn.h>
#include <string.h>
#include <input.h>
static const char cmd_red[]  = "run red.nex";
static const char cmd_blue[] = "run blue.nex";
void esx_run_red(void)  { __asm ld ix,_cmd_red
 rst 8
 defb 0x8f
__endasm; }
void esx_run_blue(void) { __asm ld ix,_cmd_blue
 rst 8
 defb 0x8f
__endasm; }
void main(void){
    memset((void*)0x4000,0x00,6144);
    memset((void*)0x5800,0x38,768);     /* white paper = MENU */
    zx_border(7);
    for(;;){
        int k = in_inkey();
        if(k=='1'){ esx_run_red();  for(;;); }
        if(k=='2'){ esx_run_blue(); for(;;); }
    }
}
