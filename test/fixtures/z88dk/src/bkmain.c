#include <stdint.h>
extern uint8_t bk_sum(uint8_t a, uint8_t b) __banked;
uint8_t result;
int main(void)
{
    uint8_t loc = 3;
    result = bk_sum(loc, 2);
    while (1) { result++; }
    return 0;
}
