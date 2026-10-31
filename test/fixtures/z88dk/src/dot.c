#include <stdint.h>
#include "dutil.h"

uint8_t total;

static uint8_t add3(uint8_t x)
{
    return x + 3;
}

int main(void)
{
    uint8_t i;
    total = 0;
    for (i = 0; i < 4; i++) {
        total = add3(total);
    }
    total = dutil_twice(total);
    return 0;
}
