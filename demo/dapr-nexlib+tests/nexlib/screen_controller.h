#ifndef __screen_controller_h__
#define __screen_controller_h__

#include <types.h>

void sc_switch_delay(u8 nframes);

void sc_switch_screen(FunPtr entry, FunPtr update, FunPtr exit);

void sc_update(void);

#endif // __screen_controller_h__
