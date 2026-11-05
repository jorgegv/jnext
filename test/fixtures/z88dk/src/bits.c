#include <stdint.h>
struct bf { unsigned lo : 3; unsigned mid : 5; unsigned hi : 8; signed int neg : 4; unsigned wide : 12; uint8_t tag; };
struct lone { unsigned a : 12; uint8_t c; };
struct one { int s : 4; };   /* plain int: unsigned on sccz80/80cc, signed on zsdcc */
struct bf g_bf;
struct lone g_lone;
struct one g_one;
int main(void)
{
    struct bf l_bf;
    l_bf.lo = 5; l_bf.mid = 17; l_bf.hi = 200; l_bf.neg = -3; l_bf.wide = 2748; l_bf.tag = 66;
    g_bf = l_bf;
    *(uint16_t *)&g_lone = 0xFFFF; g_lone.a = 2748; g_lone.c = 9;
    g_one.s = -3;
    while (1);
    return 0;
}
