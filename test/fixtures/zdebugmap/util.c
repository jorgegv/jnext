#include "util.h"

uint8_t util_twice(uint8_t v)
{
    uint8_t r;
    r = v + v;
    r = util_inc(r);
    return r;
}
