#include <stdint.h>
struct pt { int16_t x, y; };
int16_t g;
int16_t f(int16_t a)
{
    struct pt s; s.x = a; s.y = 1;
    {
        struct pt s; s.x = 50; s.y = 60;
        g += s.x + s.y;
    }
    {
        static int16_t n = 7;
        n++;
        g += n;
    }
    {
        static int16_t n = 100;
        n++;
        g += n;
    }
    return s.x + s.y;
}
int main(void) { return f(3); }
