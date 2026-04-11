///////////////////////////////////////////////////////////////////////////////
//
//  NexLib - Game Helper Library for ZX Spectrum Next
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#ifndef __print_tile_h__
#define __print_tile_h__

#include "types.h"

// button border
#define BLOCK_BUTTON_BORDER 141
#define BLOCK_BCUR 141
#define BLOCK_BSTR 142

// board border
#define BLOCK_BOARD_BORDER 143
#define BLOCK_DCUR 143
#define BLOCK_DSTR 144

#define BLOCK_FIX1 145
#define BLOCK_FIX2 146

// fine border
#define BLOCK_FINE_BORDER 147
#define BLOCK_FCUR 147
#define BLOCK_FSTR 148

// outer border
#define BLOCK_OUTER_BORDER 149
#define BLOCK_OCUR 149
#define BLOCK_OSTR 150

#define BLOCK_STAR 151

// must match print_tile.asm
#define DBT_OFFSET 128

// attributes for mirror X, mirror Y, Rotation
#define ATTR_____ 0
#define ATTR___R_ 2
#define ATTR__Y__ 4
#define ATTR__YR_ 6
#define ATTR_X___ 8
#define ATTR_X_R_ 10
#define ATTR_XY__ 12
#define ATTR_XYR_ 14


#define DBGTEXT(x,y,txt) {  \
    print_set_pos(x, y);    \
    print_str(txt);         \
}

#define DBG4X(x,y,v4) {     \
    print_set_pos(x, y);    \
    print_hex_nibble(v4);   \
}

#define DBG8X(x,y,v8) {     \
    print_set_pos(x, y);    \
    print_hex_byte(v8);     \
}

#define DBG16X(x,y,v16) {   \
    print_set_pos(x, y);    \
    print_hex_word(v16);    \
}

#define DBG4(x,y,v4) {     \
    print_set_pos(x, y);    \
    print_dec_nibble(v4);   \
}

#define DBG8(x,y,v8) {     \
    print_set_pos(x, y);    \
    print_dec_byte(v8);     \
}

#define DBG8R(x,y,v8) {       \
    print_set_pos(x, y);      \
    if(v8<100)print_char(' ');\
    if(v8< 10)print_char(' ');\
    print_dec_byte(v8);       \
}

#define DBG16(x,y,v16) {   \
    print_set_pos(x, y);    \
    print_dec_word(v16);    \
}

#define TALLY(N) DBG4X(0,N,N)

#define T0 TALLY(0)
#define T1 TALLY(1)
#define T2 TALLY(2)
#define T3 TALLY(3)
#define T4 TALLY(4)
#define T5 TALLY(5)
#define T6 TALLY(6)
#define T7 TALLY(7)
#define T8 TALLY(8)
#define T9 TALLY(9)

// print_set_pos: macro with global vars for compatibility szzc80 - sdcc
extern u8 curr_pos_x;
extern u8 curr_pos_y;
extern void print_set_pos_asm(void);
#define print_set_pos(x, y) { \
    curr_pos_x = (x); \
    curr_pos_y = (y); \
    print_set_pos_asm(); \
}

void print_set_attr(u8 attr) fastcall;
void print_set_color(u8 col) fastcall;
void print_set_symbol(u8 symbol) fastcall;

void print_symbol(void);

void print_char(char ch) fastcall;
// void print_block(u8 blkidx);
void print_str(const char* str) fastcall;
void print_hex_nibble(u8 val) fastcall;
void print_hex_byte(u8 val) fastcall;
void print_hex_word(u16 val) fastcall;
void print_dec_byte(u8 val);
void print_dec_word(u16 val);

// pos, color and symbol must be set prior to calling this
void print_frame(u8 x, u8 y, u8 w, u8 h);

void print_rect_symbol(u8 x, u8 y, u8 w, u8 h);

void print_frame_filled(u8 x, u8 y, u8 w, u8 h);

void print_set_pos_inc(sbyte x, sbyte y);
void println(const char* txt);
void println_ctr(const char* txt, u8 len);

#define UWDB_COUNT 5
extern u8 str_dec_u16[UWDB_COUNT];
extern const char* str_dec_for_u16(u16 u16arg) fastcall;

#define UBDB_COUNT 3
extern u8 str_dec_u8[UBDB_COUNT];
extern const char* str_dec_for_u8(u8 u8arg);

#endif // __print_tile_h__