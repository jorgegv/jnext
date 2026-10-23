#include <stdint.h>
#include "banked.h"

extern void bank_in(void);

uint8_t result;

int main(void)
{
#ifndef USE_BANKED_KW
    bank_in();
#endif
    result = banked_add(2, 3);
    while (1) {
        result++;
    }
    return 0;
}
