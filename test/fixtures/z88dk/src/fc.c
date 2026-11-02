#include <stdint.h>
uint16_t g;
uint16_t ffast(uint16_t x) __z88dk_fastcall { uint16_t y; y = x + 1; return y; }
uint16_t fcallee(uint16_t a, uint8_t b) __z88dk_callee { uint16_t z; z = a + b; return z; }
int main(void) { g = ffast(3); g += fcallee(g, 4); while (1); return 0; }
