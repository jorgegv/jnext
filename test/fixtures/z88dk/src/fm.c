#include <stdint.h>
int16_t g;
int16_t fm(int16_t a, int16_t b) __z88dk_fastcall
{
    int16_t y;
    y = a - b;
    return y;
}
char gs[] = "glob";
static char fs[] = "file";
int main(void) { char ls[] = "loc"; static char ss[] = "stat"; g = fm(7, 2); g += ls[0] + ss[0] + fs[0] + gs[0]; while (1); return 0; }
