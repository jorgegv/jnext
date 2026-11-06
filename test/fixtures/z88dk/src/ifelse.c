#include <stdint.h>
int16_t g;
int16_t f1(int16_t a)
{
    int16_t t = a;
    if (a > 1) {
        int16_t p = t + 1;
        g = p;
    } else {
        int16_t q = t - 1;
        g = q;
    }
    while (t < 10) {
        int16_t w = t * 2;
        t += w;
    }
    do {
        int16_t d = t;
        t -= d / 2;
    } while (t > 100);
    {
        int16_t t = 5;
        g += t;
    }
    t++;
    if (a == 3) {
        int16_t e = a;
        g -= e;
    } else if (a == 4) {
        int16_t h = a + 1;
        g -= h;
    }
    return t;
}
int16_t k9(int16_t a)
{
    int16_t t = a;
    if (a > 1)
        g = 1;
    else {
        int16_t q = t - 1;
        g = q;
    }
    return t;
}
void c1(int16_t a)
{
    int16_t t = a;
    g = t;
    if (a > 2) {
        int16_t q = t * 3;
        g = q;
    }
}
int16_t d2(int16_t a, int16_t c)
{
    int16_t t = 0;
    for (int16_t i = 0; i < 5; i++) {
        int16_t k = a * c;
        t += k + i;
    }
    return t;
}
int main(void)
{
    g = f1(3);
    k9(3);
    c1(3);
    g = d2(1, 2);
    return 0;
}
