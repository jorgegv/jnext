#include <stdint.h>
int16_t g;
int16_t f(int16_t a)
{
    int16_t t = a;
    if (a) {
        int16_t u = 1;
        {
            static int16_t t = 7;
            t++;
            g = t + u;
        }
        g += t;
    }
    t++;
    return t;
}
int16_t f2(int16_t a)
{
    if (a) {
        int16_t t = a;
        {
            static int16_t t = 3;
            g = t;
        }
        g += t;
    }
    return 0;
}
int main(void)
{
    g = f(2) + f2(1);
    return 0;
}
