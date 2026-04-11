#ifndef _interrupt_h_
#define _interrupt_h_

#include <types.h>
#include <arch/zxn.h>

/* Memory map for interrupt_init_FE

FF01 - FFFF (255 bytes) Stack space
FE00 - FF00 (257 bytes) IM2 service routine pointer (all FD bytes)
FDFD - FDFF (  3 bytes) ISR jump - JP nn [3 bytes]

*/
void interrupt_init_FE(void);

/* Memory map for interrupt_init_FD

FE01 - FFFF (511 bytes) Stack space
FD00 - FE00 (257 bytes) IM2 service routine pointer (all FC bytes)
FCFC - FCFF (  4 bytes) ISR jump - JP nn [3 bytes]; NOP

*/
void interrupt_init_FD(void);

// assign this variable to the interrupt callback
extern FunPtr on_interrupt_callback;

// set line interrupt only, on given scanline
#define interrupt_setup_scanline_only(SCANLINE) \
{ \
    ZXN_NEXTREG(0x22, 6);          /* disable ULA interrupt, enable Line interrupt */ \
    ZXN_NEXTREG(0x23, SCANLINE);   /* request line interrupt at given scanline */ \
}

// get current line interrupt
#define interrupt_get_scanline() (ZXN_READ_REG_fastcall(0x23))

// vector address for debugging
extern void* interrupt_isr_vector;



#endif