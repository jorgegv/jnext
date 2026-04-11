#include "interrupt.h"

#include <im2.h>
#include <z80.h>
#include <intrinsic.h>
#include <string.h>
#include <arch/zxn.h>


static void dummy_cb(void) {}

FunPtr on_interrupt_callback = dummy_cb;

IM2_DEFINE_ISR(update_im2)
{
    on_interrupt_callback();
}

u8 scanline_interrupt = 0;

void* interrupt_isr_vector = 0;




















// void interrupt_init_FD(void)
// {
    
//     interrupt_init_FD_asm();


//     intrinsic_di();

//     //FD
//     // FC00 - FD00 : FD

//     im2_init((void *) 0xFD00);
//     im2_init(IM2_FD_SEGMENT);
//     // memset((void *) 0xFD00, 0xFC, 257);
//     // z80_bpoke(0xFCFC, 0xC3);
//     z80_wpoke(0xFCFD, (uint16_t) update_im2);

//     // im2_init((void *) 0xFC00);
//     // memset((void *) 0xFC00, 0xFD, 257);
//     // z80_bpoke(0xFDFD, 0xC3);
//     // z80_wpoke(0xFDFE, (uint16_t) update_im2);


//     intrinsic_ei();


// }