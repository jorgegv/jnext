///////////////////////////////////////////////////////////////////////////////
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#ifndef __zxkeyb_h__
#define __zxkeyb_h__

#include "types.h"

void keyb_init(void);
void keyb_update(void);

void keyb_print_debug(u8 x, u8 y);
void keyb_codes_debug(u8 x, u8 y);

bool keyb_is_pressed(u16 keycode) fastcall;
bool keyb_was_pressed(u16 keycode) fastcall;

bool keyb_is_just_pressed (u16 keycode) fastcall;
bool keyb_is_just_released(u16 keycode) fastcall;

extern u8 zxkey54321;
extern u8 zxkey67890;
extern u8 zxkeyTREWQ;
extern u8 zxkeyYUIOP;
extern u8 zxkeyGFDSA;
extern u8 zxkeyHJKLe;
extern u8 zxkeyVCXZc;
extern u8 zxkeyBNMys;
extern u8 nxkey0;
extern u8 nxkey1;

// returns true if any key is pressed
bool keyb_is_pressed_any(void);
// returns true if any key was pressed
bool keyb_was_pressed_any(void);
// returns true if any key is just pressed
bool keyb_is_just_pressed_any(void);
// returns true if any key is just pressed
bool keyb_is_just_released_any(void);

// returns number of keys pressed
u8 keyb_count(void);

// returns code of pressed key, 0 if none pressed
// if more than one key is pressed,
// the result is undefined.
u16 keyb_code(void);

// returns short/compact version of code
u8 keyb_short_for_code(u16 code) fastcall;

// character for short key code
extern const char keyb_ch4short[];

#define KEY_ZX_1   0x0401
#define KEY_ZX_2   0x0402
#define KEY_ZX_3   0x0404
#define KEY_ZX_4   0x0408
#define KEY_ZX_5   0x0410

#define KEY_ZX_0   0x0301
#define KEY_ZX_9   0x0302
#define KEY_ZX_8   0x0304
#define KEY_ZX_7   0x0308
#define KEY_ZX_6   0x0310

#define KEY_ZX_Q   0x0501
#define KEY_ZX_W   0x0502
#define KEY_ZX_E   0x0504
#define KEY_ZX_R   0x0508
#define KEY_ZX_T   0x0510

#define KEY_ZX_P   0x0201
#define KEY_ZX_O   0x0202
#define KEY_ZX_I   0x0204
#define KEY_ZX_U   0x0208
#define KEY_ZX_Y   0x0210

#define KEY_ZX_A   0x0601
#define KEY_ZX_S   0x0602
#define KEY_ZX_D   0x0604
#define KEY_ZX_F   0x0608
#define KEY_ZX_G   0x0610

#define KEY_ZX_ENT 0x0101
#define KEY_ZX_L   0x0102
#define KEY_ZX_K   0x0104
#define KEY_ZX_J   0x0108
#define KEY_ZX_H   0x0110

#define KEY_ZX_CAP 0x0701
#define KEY_ZX_Z   0x0702
#define KEY_ZX_X   0x0704
#define KEY_ZX_C   0x0708
#define KEY_ZX_V   0x0710

#define KEY_ZX_SPC 0x0001
#define KEY_ZX_SYM 0x0002
#define KEY_ZX_M   0x0004
#define KEY_ZX_N   0x0008
#define KEY_ZX_B   0x0010

#define KEY_NX_SEMICOLON 0x0880
#define KEY_NX_QUOTE     0x0840
#define KEY_NX_COMMA     0x0820
#define KEY_NX_PERIOD    0x0810
#define KEY_NX_UP        0x0808
#define KEY_NX_DOWN      0x0804
#define KEY_NX_LEFT      0x0802
#define KEY_NX_RIGHT     0x0801

#define KEY_NX_DEL      0x0980
#define KEY_NX_EDIT     0x0940
#define KEY_NX_BREAK    0x0920
#define KEY_NX_INVVIDEO 0x0910
#define KEY_NX_TRUVIDEO 0x0908
#define KEY_NX_GRAPH    0x0904
#define KEY_NX_CAPSLOCK 0x0902
#define KEY_NX_EXTEND   0x0901

// bool zxkey1();
// bool zxkey2();
// bool zxkey3();
// bool zxkey4();
// bool zxkey5();
// bool zxkey6();
// bool zxkey7();
// bool zxkey8();
// bool zxkey9();
// bool zxkey0();

// bool zxkeyQ();
// bool zxkeyW();
// bool zxkeyE();
// bool zxkeyR();
// bool zxkeyT();
// bool zxkeyY();
// bool zxkeyU();
// bool zxkeyI();
// bool zxkeyO();
// bool zxkeyP();

// bool zxkeyA();
// bool zxkeyS();
// bool zxkeyD();
// bool zxkeyF();
// bool zxkeyG();
// bool zxkeyH();
// bool zxkeyJ();
// bool zxkeyK();
// bool zxkeyL();
// bool zxkeyENT();

// bool zxkeyCAP();
// bool zxkeyZ();
// bool zxkeyX();
// bool zxkeyC();
// bool zxkeyV();
// bool zxkeyB();
// bool zxkeyN();
// bool zxkeyM();
// bool zxkeySYM();
// bool zxkeySPC();

// bool nxkeySemicolon();
// bool nxkeyQuote();
// bool nxkeyComma();
// bool nxkeyPeriod();
// bool nxkeyUp();
// bool nxkeyDown();
// bool nxkeyLeft();
// bool nxkeyRight();

// bool nxkeyDel();
// bool nxkeyEdit();
// bool nxkeyBreak();
// bool nxkeyInvVideo();
// bool nxkeyTruVideo();
// bool nxkeyGraph();
// bool nxkeyCapsLock();
// bool nxkeyExtend();


#endif // _zxkeyb_h_
