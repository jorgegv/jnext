#include <stdint.h>
const uint8_t btable[4] = { 10, 20, 30, 40 };
uint8_t bdata = 77;
uint8_t bk_sum(uint8_t a, uint8_t b) __banked
{
    uint8_t s;
    uint8_t j;
    s = a;
    for (j = 0; j < 4; j++) s += btable[j];
    return s + b;
}
