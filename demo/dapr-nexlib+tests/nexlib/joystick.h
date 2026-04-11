///////////////////////////////////////////////////////////////////////////////
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#ifndef __joystick_h__
#define __joystick_h__

#include "types.h"

// joycodes
#define JOY_UP    0x08
#define JOY_DOWN  0x04
#define JOY_LEFT  0x02
#define JOY_RIGHT 0x01
#define JOY_BUT1  0x10
#define JOY_BUT2  0x20
#define JOY_BUT3  0x40
#define JOY_BUT4  0x80

extern u8 joydata;  // raw joystick data from this frame
extern u8 joyprev;  // raw joystick data from previous frame

// initialize joystick
void joystick_init(void);

// update - call every frame before calling functions below
void joystick_update(void);

bool joystick_is_pressed(u8 joycode) fastcall;
bool joystick_was_pressed(u8 joycode) fastcall;

bool joystick_is_just_pressed (u8 joycode) fastcall;
bool joystick_is_just_released(u8 joycode) fastcall;

// returns true if any joystick dir/btn is pressed
bool joystick_is_pressed_any(void);
bool joystick_was_pressed_any(void);

bool joystick_is_just_pressed_any(void);
bool joystick_is_just_released_any(void);

// returns number of joystick dir/btns pressed
u8 joystick_count(void);

// returns code of pressed joystick dir/btn, 0 if none pressed
// if more than one is pressed,
// the result is undefined.
u8 joystick_code(void);

// returns short/compact version of code
u8 joystick_short_for_code(u8 code) fastcall;

// character for short key code
extern const char joystick_ch4short[];

////////////////////////////////////////////////////////////

// update joystick flags
void joystick_update_flags(void);

// these flags will be updated only if joystick_update_flags() is called
extern u8 joyLeft;  // left
extern u8 joyRight; // right
extern u8 joyUp;    // up
extern u8 joyDown;  // down
extern u8 joyBut1;  // button 1
extern u8 joyBut2;  // button 2
extern u8 joyBut3;  // button 1
extern u8 joyBut4;  // button 2


#endif