#include <stdint.h>
#include "util.h"

extern void asm_bump(void);

uint8_t counter;
static uint8_t table[8];

static uint8_t square(uint8_t x)
{
    return x * x;
}

int main(void)
{
    uint8_t i;
    counter = 0;
    for (i = 0; i < 8; i++) {
        table[i] = square(i);
        counter += table[i];
    }
    if (counter > 100) counter = UTIL_CLAMP(counter, 100);
    asm_bump();
    counter = util_twice(counter);
    while (1) {
        counter++;
    }
    return 0;
}
