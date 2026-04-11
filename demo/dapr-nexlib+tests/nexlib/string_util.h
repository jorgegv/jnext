#ifndef _string_util_h_
#define _string_util_h_

#include <types.h>

extern const char* str_hex_u8;
const char* str_hex_for_u8(u8 u8val) fastcall;

extern const char* str_hex_u16;
const char* str_hex_for_u16(u16 u16val) fastcall;

extern const char* str_hex_u8;
const char* str_bin_for_u8(u8 u8val) fastcall;

extern const char* str_bin_u16;
const char* str_bin_for_u16(u16 u16val) fastcall;

char chr_hex_for_u4(u8 u4val) fastcall;

#endif