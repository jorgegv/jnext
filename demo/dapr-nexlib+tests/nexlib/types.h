///////////////////////////////////////////////////////////////////////////////
//
//  (c) 2025 David Crespo - https://github.com/dcrespo3d
//                          https://davidprograma.itch.io
//                          https://www.youtube.com/@Davidprograma
//
///////////////////////////////////////////////////////////////////////////////

#ifndef __types_h__
#define __types_h__

// for bool
#if defined(__SDCC) || defined(__SCCZ80)
#include <stdbool.h>
#else
#define bool u8
#define true 1
#define false 0
#endif
// for NULL and others
#include <stddef.h>

// unsigned byte
typedef unsigned char ubyte;
typedef unsigned char u8;
// unsigned word
typedef unsigned short uword;
typedef unsigned short u16;

// signed byte
typedef signed char sbyte;
typedef signed char s8;
// signed word
typedef signed short sword;
typedef signed short s16;

// extract high nibble of a byte
#define HINIB(a_byte) (a_byte >> 4)

// extract low nibble of a byte
#define LONIB(a_byte) (a_byte & 0xF)

// join two nibbles into a byte (unchecked)
#define JOINIB(hi, lo) ((hi << 4) | lo)

// FunPtr is a pointer to a function accepting no arguments
// and returning void
typedef void (*FunPtr)(void);

// custom fastcall function decorator
#if defined(__SDCC) || defined(__SCCZ80)
#define fastcall __z88dk_fastcall
#else
#define fastcall
#endif
// example:
// u8 myfunc(u8* arg) fastcall;
// Note: fastcall only supports one parameter in DEHL
// L = 8 bit
// HL = 16 bit
// DEHL = 32 bit

#if defined(__SDCC) || defined(__SCCZ80)
#define callee __z88dk_callee
#else
#define callee
#endif

#define stdcall

#endif // __types_h__
