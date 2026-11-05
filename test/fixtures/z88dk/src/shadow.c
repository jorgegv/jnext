#include <stdint.h>
int16_t g;
int16_t f(int16_t a)
{
    int16_t t = a;
    {
        int16_t t = 5;
        g += t;
    }
    t++;
    return t;
}
int main(void) { return f(3); }
