#include "types.h"

static uint8_t s_dup = 4;

int16_t other(int16_t q)
{
    int16_t res;
    res = q * 2 + s_dup;
    return res;
}
