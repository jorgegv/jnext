#include <stdint.h>
int16_t g;
int16_t f(int16_t a)
{
    int16_t t = a;
    if (a > 0) {
        int16_t p = t * 2;
        t += p;
    }
    for (int16_t i = 0; i < 3; i++) {
        t += i;
    }
    {
        int16_t u = t;
        {
            int16_t v = u;
            t += v;
        }
        int16_t z = u + 1;
        t += z;
    }
    return t;
}
int16_t g2(int16_t a)
{
    if (a) {
        int16_t q = a;
        g = q;
    }
    for (;;) {
        int16_t w = g;
        if (w) break;
    }
    return g;
}
int16_t h(int16_t a)
{
    int16_t t = a;
    {
        int16_t m = t;
        for (int16_t j = 0; j < 2; j++) {
            t += j;
        }
        t += m;
    }
    return t;
}
int main(void) { g = f(3); g += g2(4); g += h(5); while (1); return 0; }
