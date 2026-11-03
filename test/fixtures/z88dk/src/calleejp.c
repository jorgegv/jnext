#include <sound.h>

extern unsigned int addtwo(unsigned int a, unsigned int b) __z88dk_callee;

unsigned int twice(unsigned int v)
{
    unsigned int r;
    r = addtwo(v, v);
    r = r + 1;
    return r;
}

void beep(void)
{
    bit_beep_raw(3, 100);
    bit_beep_raw(3, 100);
}

int main(void)
{
    unsigned int t;
    t = twice(21);
    beep();
    return t;
}
