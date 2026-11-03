#include <stdint.h>
#include "banked.h"

/* Linked into PAGE_20 or BANK_10 by generate.sh (zcc --codeseg). */
uint8_t banked_add(uint8_t a, uint8_t b) BANKED_FN
{
    uint8_t s;
    s = a + b;
    return s;
}
