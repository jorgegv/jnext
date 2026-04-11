#include "mathutil.h"

u16 sign16(s16 val) fastcall
{
    if (val < 0) return -1;
    if (val > 0) return 1;
    return 0;
}

s8 sign8(s8 val) fastcall
{
    if (val < 0) return -1;
    if (val > 0) return 1;
    return 0;
}

u16 abs16(s16 val) fastcall
{
    if (val < 0) return -val;
    return val;
}

u8 abs8(s8 val) fastcall
{
    if (val < 0) return -val;
    return val;
}

u8 max8(u8 a, u8 b)
{
    if (a > b) return a;
    return b;
}

u8 min8(u8 a, u8 b)
{
    if (a < b) return a;
    return b;
}

u16 max16(u16 a, u16 b)
{
    if (a > b) return a;
    return b;
}

u16 min16(u16 a, u16 b)
{
    if (a < b) return a;
    return b;
}