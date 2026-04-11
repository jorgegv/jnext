#include "screen_controller.h"

FunPtr sc_curr_entry  = NULL;
FunPtr sc_curr_update = NULL;
FunPtr sc_curr_exit   = NULL;

FunPtr sc_next_entry = NULL;
FunPtr sc_next_update = NULL;
FunPtr sc_next_exit = NULL;

ubyte sc_delay_frames = 0;

void sc_switch_delay(ubyte nframes)
{
    sc_delay_frames = nframes;
}

void sc_switch_screen(FunPtr entry, FunPtr update, FunPtr exit)
{
    sc_next_entry  = entry;
    sc_next_update = update;
    sc_next_exit   = exit;
}

void sc_update(void)
{
    // perform update for current screen
    if (sc_curr_update) sc_curr_update();

    if (sc_delay_frames) {
        sc_delay_frames--;
        return;
    }

    if (sc_next_update) {
        // exit from previous screen, if any
        if (sc_curr_exit) sc_curr_exit();

        // assign function pointers
        sc_curr_entry  = sc_next_entry;
        sc_curr_update = sc_next_update;
        sc_curr_exit   = sc_next_exit;
        
        sc_next_entry  = NULL;
        sc_next_update = NULL;
        sc_next_exit   = NULL;

        // stop all sound before entering any screen
        // snd_panic();

        // enter next (current) screen, if any
        if (sc_curr_entry) sc_curr_entry();
    }
}

