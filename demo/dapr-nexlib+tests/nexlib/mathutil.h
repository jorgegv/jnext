#ifndef _mathutil_h_
#define _mathutil_h_

#include <types.h>

#define ABS(x) ((x)<(0)?(-(x)):(x))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define APPR(val, target, amount) (((val)>(target))?(MAX(((val)-(amount)),(target))):(MIN(((val)+(amount)),(target))))
#define SIGN(x) ((x)==(0)?(0):((x)<(0)?(-1):(1)))

// returns sign(param)*target
#define APPLY_SIGN(param,target) ((param)<(0)?(-(target)):((param)>(0)?(target):(0)))

// bit utilities
#define BITSET(val,bitmask) (val |=  (bitmask))
#define BITCLR(val,bitmask) (val &= ~(bitmask))
#define BITTST(val,bitmask) (val &   (bitmask))

u16 sign16(s16 val) fastcall;
u16 abs16(s16 val) fastcall;
u16 max16(u16 a, u16 b);
u16 min16(u16 a, u16 b);

s8 sign8(s8 val) fastcall;
u8 abs8(s8 val) fastcall;
u8 max8(u8 a, u8 b);
u8 min8(u8 a, u8 b);

#endif

