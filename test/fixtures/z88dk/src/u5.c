#include <stdint.h>
uint8_t g;
uint8_t twice(uint8_t v)
{
    uint8_t r;
    r = v + v;
    r = r + 1;
    return r;
}
int main(void)
{
    uint8_t loc = 3;
    g = twice(loc);
    return 0;
}
