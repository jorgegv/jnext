// A C function whose body crosses an 8 KB slot (GH #434 F-A): the padding is 8192 bytes of zeros
// (NOPs), so the lines after it are past the next 8 KB boundary of the code.
#include <stdint.h>

int16_t g_x;

int16_t cross(int16_t a)
{
    int16_t r;
    r = a + 1;
#ifdef __SDCC
    __asm
    defs 8192
    __endasm;
#else
#asm
    defs 8192
#endasm
#endif
    r = r * 2;
    return r;
}

int main(void)
{
    g_x = cross(20);
    while (1)
        ;
    return 0;
}
