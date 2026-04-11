///////////////////////////////////////////////////////////////////////////////
//
//  NexLib - Game Helper Library for ZX Spectrum Next
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#include "print_tile.h"
#include <string.h>

extern ubyte curr_print_symbol;
extern ubyte last_pos_x;
extern ubyte last_pos_y;

void print_set_pos_inc(sbyte x, sbyte y)
{
    print_set_pos(last_pos_x + x, last_pos_y + y);
}

void print_rect_symbol(ubyte x, ubyte y, ubyte w, ubyte h)
{
    for (ubyte j = 0; j < h; j++) {
        print_set_pos(x, y++);
        for (ubyte i = 0; i < w; i++) {
            print_symbol();
        }
    }   
}

void print_frame_filled(ubyte x, ubyte y, ubyte w, ubyte h)
{
    ubyte oldsymbol = curr_print_symbol;
    curr_print_symbol = (ubyte)(DBT_OFFSET + ' ');
    print_rect_symbol(x+1, y+1, w-2, h-2);
    curr_print_symbol = oldsymbol;
    print_frame(x, y, w, h);
    print_set_pos(x+1, y+1);
}

void println(const char* txt)
{
    print_str(txt);
    print_char('\n');
}

void println_ctr(const char* txt, ubyte len)
{
    ubyte tlen = strlen(txt);
    ubyte dx = len - tlen;
    if (dx > len) dx = 0;
    dx >>= 1;
    ubyte old_pos_x = last_pos_x;
    print_set_pos(last_pos_x + dx, last_pos_y);
    print_str(txt);
    last_pos_x = old_pos_x;
    print_char('\n');
}


/*

ubyte* tmstart = 0x4000;
ubyte* curr_print_posptr = tmstart;
ubyte curr_print_attr = 0;
ubyte curr_print_symbol = 0;

ubyte last_pos_x = 0;
ubyte last_pos_y = 0;

void print_set_pos_inc(sbyte x, sbyte y)
{
    print_set_pos(last_pos_x + x, last_pos_y + y);
}

void print_set_pos(ubyte x, ubyte y)
{
    // curr_print_posptr = tmstart;
    // curr_print_posptr += 80 * y;
    // curr_print_posptr += 2 * x;
__asm
    LD  HL, 4
    ADD HL, SP
    LD  A, (HL)  ;x
    LD (_last_pos_x), A
    LD  B, A
    
    LD  HL, 2
    ADD HL, SP
    LD  A, (HL)  ;y
    LD (_last_pos_y), A
    LD  C, A

    LD  H, 0
    LD  L, C
    ADD HL, HL
    ADD HL, HL
    ADD HL, HL
    ADD HL, HL
    LD  DE, HL
    ADD HL, HL
    ADD HL, HL
    ADD HL, DE   ; 80*y
    LD  DE, HL

    LD  H, 0
    LD  L, B
    ADD HL, HL   ; 2*x
    ADD HL, DE   ; 2*x + 80*y
    LD  DE, HL

    LD  HL, (_tmstart)
    ADD HL, DE
    LD  (_curr_print_posptr), HL
__endasm
}

void print_set_attr(ubyte attr)
{
    // curr_print_attr = attr;
    __asm
    LD HL, 2
    ADD HL, SP
    LD A, (HL)

    LD (_curr_print_attr), A
    __endasm
}

void print_set_color(ubyte col)
{
    // curr_print_attr = col << 4;
    __asm
    LD HL, 2
    ADD HL, SP
    LD A, (HL)

    SWAPNIB
    AND A, %11110000

    LD (_curr_print_attr), A

    __endasm
}

void print_set_symbol(ubyte symbol)
{
    // curr_print_attr = attr;
    __asm
    LD HL, 2
    ADD HL, SP
    LD A, (HL)

    LD (_curr_print_symbol), A
    __endasm
}

void print_symbol()
{
    // *curr_print_posptr++ = curr_print_symbol;
    // *curr_print_posptr++ = curr_print_attr;
    __asm
    LD  HL, (_curr_print_posptr)
    LD  A, (_curr_print_symbol)
    LD (HL), A
    INC HL

    LD  A, (_curr_print_attr)
    LD (HL), A
    INC HL

    LD (_curr_print_posptr), HL

    __endasm
}

void print_char(char ch)
{
    // if (ch >= 96) ch -= 32;
    // *curr_print_posptr++ = DBT_OFFSET + ch;
    // *curr_print_posptr++ = curr_print_attr;
    __asm
    LD HL, 2
    ADD HL, SP
    LD L, (HL)
    LD A, L

    CP A, '\n'
    JR NZ, print_char_char

    LD A, (_last_pos_x)
    LD L, A
    PUSH HL

    LD A, (_last_pos_y)
    INC A
    LD H, 0
    LD L, A
    PUSH HL

    CALL _print_set_pos
    POP HL
    POP HL

    RET
    
    // SUB A, 96
    // LD A, L
    // JR C, print_char_ge96
    // SUB A, 32

    // print_char_ge96:

print_char_char:
    LD HL, (_curr_print_posptr)
    ADD A, DBT_OFFSET
    LD (HL), A
    INC HL

    LD A, (_curr_print_attr)
    LD (HL), A
    INC HL

    LD (_curr_print_posptr), HL

    __endasm
}

void print_block(ubyte blkidx)
{
    __asm
    LD HL, 2
    ADD HL, SP
    LD L, (HL)
    LD A, L
    
    LD HL, (_curr_print_posptr)
    LD (HL), A
    INC HL

    LD A, (_curr_print_attr)
    LD (HL), A
    INC HL

    LD (_curr_print_posptr), HL

    __endasm
}

void print_str(const char* str)
{
    // char ch = *str++;
    // while (ch != 0)
    // {
    //     print_char(ch);
    //     ch = *str++;
    // }
    __asm
    POP BC
    POP HL
    PUSH HL
    PUSH BC
    LD A, (HL)
    CP 0
    JR Z, print_str_end
    print_str_loop:
        LD D, 0
        LD E, A
        PUSH HL
        PUSH DE
        CALL _print_char
        POP DE
        POP HL
        INC HL
        LD A, (HL)
        CP 0
    JR NZ, print_str_loop

    print_str_end:
    __endasm
}

char hex_char_for_val(char val) {
    // char ch;
    // if (val < 10) return '0' + val;
    // else          return '7' + val;
    // return ch;
    __asm
    LD HL, 2
    ADD HL, SP
    LD L, (HL)

    LD A, L
    SUB A, 10
    LD A, L
    JR C, hex_char_for_val_ge10
    ADD A, 7

    hex_char_for_val_ge10:
    ADD A, 0x30     ; + '0'
    LD H, 0
    LD L, A
    __endasm
}



void print_hex_word(uword val)
{
    // char ch1, ch2, ch3, ch4;
    // ch1 = hex_char_for_val(val & 0xF);
    // val >>= 4;
    // ch2 = hex_char_for_val(val & 0xF);
    // val >>= 4;
    // ch3 = hex_char_for_val(val & 0xF);
    // val >>= 4;
    // ch4 = hex_char_for_val(val & 0xF);
    // print_char(ch4);
    // print_char(ch3);
    // print_char(ch2);
    // print_char(ch1);
    __asm
    POP BC
    POP HL
    PUSH HL
    PUSH BC
    
    LD  A, H
    SWAPNIB
    AND A, %00001111
    LD  E, A

    LD  A, H
    AND A, %00001111
    LD  D, A

    LD  A, L
    SWAPNIB
    AND A, %00001111
    LD  C, A

    LD  A, L
    AND A, %00001111
    LD  B, A

    ; PUSH DE   ; not needed as called routines do not clobber DE
    ; PUSH BC   ; not needed as called routines do not clobber BC
    LD H, 0
    LD L, E
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP
    ; POP BC   ; not needed as called routines do not clobber BC
    ; POP DE   ; not needed as called routines do not clobber DE

    LD H, 0
    LD L, D
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP

    LD H, 0
    LD L, C
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP

    LD H, 0
    LD L, B
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP

    __endasm
}

ubyte print_dec_word_flag;

void print_dec_word(uword val)
{
    __asm
    POP BC
    POP HL      ; HL <- val
    PUSH HL
    PUSH BC

    XOR A
    CP H
    JR NZ, print_dec_word_0
    CP L
    JR NZ, print_dec_word_0
    LD L, '0'
    PUSH HL
    CALL _print_char
    POP HL
    RET

print_dec_word_0:
    XOR A
    LD (_print_dec_word_flag), A

    LD BC, -10000
    CALL print_dec_word_1
    LD BC, -1000
    CALL print_dec_word_1
    LD BC, -100
    CALL print_dec_word_1
    LD C, -10
    CALL print_dec_word_1
    LD C, B

print_dec_word_1:
    LD A, '0' - 1
print_dec_word_2:
    INC A
    ADD HL, BC
    JR  C, print_dec_word_2
    SBC HL, BC

    LD D, A
    LD A, (_print_dec_word_flag)
    CP A, 1
    LD A, D
    JR Z, print_dec_word_noskip

    CP A, '0'
    JR Z, print_dec_word_skip

print_dec_word_noskip:
    LD D,0
    LD E,A
    PUSH HL
    // PUSH BC     ; not needed as called routines do not clobber BC
    PUSH DE
    CALL _print_char
    POP DE
    // POP BC      ; not needed as called routines do not clobber BC
    POP HL

    LD A, 1
    LD (_print_dec_word_flag), A

print_dec_word_skip:    
    
    __endasm
}

void print_hex_byte(ubyte val)
{
    // char ch1, ch2;
    // ch1 = hex_char_for_val(val & 0xF);
    // val >>= 4;
    // ch2 = hex_char_for_val(val & 0xF);
    // val >>= 4;
    // print_char(ch2);
    // print_char(ch1);
    __asm
    LD  HL, 2
    ADD HL, SP
    LD  L, (HL)

    LD  A, L
    SWAPNIB
    AND A, %00001111
    LD  C, A

    LD  A, L
    AND A, %00001111
    LD  B, A

    ; PUSH BC   ; not needed as called routines do not clobber BC
    LD H, 0
    LD L, C
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP
    ; POP BC   ; not needed as called routines do not clobber BC

    LD H, 0
    LD L, B
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP

    __endasm
}

void print_dec_byte(ubyte val)
{
    __asm
    LD  HL, 2
    ADD HL, SP
    LD  A, (HL)

    CP  A, 0
    JR  NZ, print_dec_byte_0
    LD  H, 0
    LD  L, '0'
    PUSH HL
    CALL _print_char
    POP  HL
    RET

print_dec_byte_0:
    LD H, 0

    LD C, -100
    CALL print_dec_byte_1
    LD C, -10
    CALL print_dec_byte_1
    LD C, -1

print_dec_byte_1:
    LD B, '0' - 1
print_dec_byte_2:
    INC B
    ADD A, C
    JR  C, print_dec_byte_2
    SBC A, C

    LD  D, A
    LD  A, H
    CP  A, 1
    LD  A, D
    JR  Z, print_dec_byte_noskip

    LD  D, A
    LD  A, B
    CP  A, '0'
    LD  A, D
    JR  Z, print_dec_byte_skip

print_dec_byte_noskip:
    LD  L, A
    LD  A, B
    LD  D, 0
    LD  E, A
    PUSH HL
    PUSH DE
    CALL _print_char
    POP DE
    POP HL
    LD  A, L

    LD  H, 1

print_dec_byte_skip:

    __endasm
}

void print_hex_nibble(ubyte val)
{
    __asm
    LD  HL, 2
    ADD HL, SP
    LD  L, (HL)

    LD  A, L
    AND A, %00001111
    LD  B, A

    LD H, 0
    LD L, B
    PUSH HL
    CALL _hex_char_for_val
    INC SP
    INC SP
    PUSH HL
    CALL _print_char
    INC SP
    INC SP

    __endasm
}

ubyte* curr_frame_origin;

void print_frame(ubyte x0, ubyte y0, ubyte w, ubyte h)
{
#define print_frame_ASM
#ifndef print_frame_ASM
    print_set_pos(x0, y0);
    curr_frame_origin = curr_print_posptr;

    // top-left corner    
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR_____;
    print_symbol();

    // top border
    curr_print_symbol++;
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR___R_;
    for (ubyte i = 2; i < w; i++)
        print_symbol();

    // top-right corner
    curr_print_symbol--;
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR_X___;
    print_symbol();

    // right border
    curr_print_symbol++;
    // curr_print_attr &= 0xF0; curr_print_attr |= ATTR_X___;   // redundant
    for (ubyte j = 2; j < h; j++) {
        curr_print_posptr += 78;
        print_symbol();
    }

    // bottom-right corner
    curr_print_symbol--;
    curr_print_posptr += 78;
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR_XY__;
    print_symbol();

    // back to top-left corner
    curr_print_posptr = curr_frame_origin;

    // left border
    curr_print_symbol++;
    curr_print_posptr += 2;
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR_____;
    for (ubyte j = 2; j < h; j++) {
        curr_print_posptr += 78;
        print_symbol();
    }

    // bottom-left corner
    curr_print_symbol--;
    curr_print_posptr += 78;
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR__Y__;
    print_symbol();

    // bottom border
    curr_print_symbol++;
    curr_print_attr &= 0xF0; curr_print_attr |= ATTR__YR_;
    for (ubyte i = 2; i < w; i++)
        print_symbol();

    curr_print_attr &= 0xF0;

#else
__asm
    LD  HL, 8
    ADD HL, SP
    LD  L, (HL)
    LD  H, 0
    PUSH HL

    LD  HL, 8
    ADD HL, SP
    LD  L, (HL)
    LD  H, 0
    PUSH HL

    CALL _print_set_pos
    POP HL
    POP HL

    LD  HL, 4
    ADD HL, SP
    LD  A, (HL)
    SUB A, 2
    LD  E, A    ; E = w-2

    LD  HL, 2
    ADD HL, SP
    LD  A, (HL)
    SUB A, 2
    LD  D, A    ; D = h-2

    LD HL, (_curr_print_posptr)
    LD (_curr_frame_origin), HL

    ; top-left corner
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR_____
    LD (_curr_print_attr), A
    CALL _print_symbol

    ; top border
    LD  A, (_curr_print_symbol)
    INC A
    LD  (_curr_print_symbol), A
    
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR___R_
    LD (_curr_print_attr), A

    LD A, E
    LD B, A
print_frame_top_border:
    CALL _print_symbol
    DJNZ print_frame_top_border

    ; top-right corner
    LD  A, (_curr_print_symbol)
    DEC A
    LD  (_curr_print_symbol), A
    
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR_X___
    LD (_curr_print_attr), A

    CALL _print_symbol

    ; right border
    LD  A, (_curr_print_symbol)
    INC A
    LD  (_curr_print_symbol), A
    
    ; redundant
    ; LD  A, (_curr_print_attr)
    ; AND A, $F0
    ; OR  A, ATTR_X___
    ; LD (_curr_print_attr), A

    LD  A, D
    LD  B, A
    PUSH DE
    LD  DE, 78
print_frame_right_border:
    LD  HL, (_curr_print_posptr)
    ADD HL, DE
    LD  (_curr_print_posptr), HL
    CALL _print_symbol
    DJNZ print_frame_right_border
    POP DE

    ; bottom-right corner
    LD  A, (_curr_print_symbol)
    DEC A
    LD  (_curr_print_symbol), A
    
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR_XY__
    LD (_curr_print_attr), A

    PUSH DE
    LD  DE, 78
    LD  HL, (_curr_print_posptr)
    ADD HL, DE
    LD  (_curr_print_posptr), HL
    CALL _print_symbol
    POP DE

    ; back to top-left corner
    LD HL, (_curr_frame_origin)
    LD (_curr_print_posptr), HL

    ; right border
    LD  A, (_curr_print_symbol)
    INC A
    LD  (_curr_print_symbol), A
    
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR_____
    LD (_curr_print_attr), A

    LD  A, D
    LD  B, A
    PUSH DE
    LD  DE, 78
    LD  HL, (_curr_print_posptr)
    INC HL
    INC HL
    LD  (_curr_print_posptr), HL
print_frame_left_border:
    LD  HL, (_curr_print_posptr)
    ADD HL, DE
    LD  (_curr_print_posptr), HL
    CALL _print_symbol
    DJNZ print_frame_left_border
    POP DE

    ; bottom-left corner
    LD  A, (_curr_print_symbol)
    DEC A
    LD  (_curr_print_symbol), A
    
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR__Y__
    LD (_curr_print_attr), A

    PUSH DE
    LD  DE, 78
    LD  HL, (_curr_print_posptr)
    ADD HL, DE
    LD  (_curr_print_posptr), HL
    CALL _print_symbol
    POP DE

    ; bottom border
    LD  A, (_curr_print_symbol)
    INC A
    LD  (_curr_print_symbol), A
    
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR__YR_
    LD (_curr_print_attr), A

    LD A, E
    LD B, A
print_frame_bottom_border:
    CALL _print_symbol
    DJNZ print_frame_bottom_border

    ; restore attribute
    LD  A, (_curr_print_attr)
    AND A, $F0
    OR  A, ATTR_____
    LD (_curr_print_attr), A

__endasm
#endif
}




// ubyte x1 = x0 + w - 1;
// ubyte y1 = y0 + h - 1;

// tilemap_put_free_tile(x0, y0, BLOCK_BCOR, ATTR_____);
// tilemap_put_free_tile(x0, y1, BLOCK_BCOR, ATTR__Y__);
// tilemap_put_free_tile(x1, y0, BLOCK_BCOR, ATTR_X___);
// tilemap_put_free_tile(x1, y1, BLOCK_BCOR, ATTR_XY__);

// for (ubyte j = y0+1; j < y1; j++) {
//     tilemap_put_free_tile(x0, j, BLOCK_BSTR, ATTR_____);
//     tilemap_put_free_tile(x1, j, BLOCK_BSTR, ATTR_X___);
// }
// for (ubyte i = x0+1; i < x1; i++) {
//     tilemap_put_free_tile(i, y0, BLOCK_BSTR, ATTR___R_);
//     tilemap_put_free_tile(i, y1, BLOCK_BSTR, ATTR__YR_);
// }
*/