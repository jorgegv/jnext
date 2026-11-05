#include <stdint.h>
/* GH #434 stage 2 (P2): float constants whose bytes are the oracle of the C value formatter.
   sccz80/80cc store them in z88dk's 48-bit format ({6}SF), zsdcc as IEEE binary32 ({4}SF). */
float f_15 = 1.5;
float f_m25 = -2.5;
float f_01 = 0.1;
float f_1e10 = 1e10;
float f_1em10 = 1e-10;
float f_0 = 0;

int main(void)
{
    return 0;
}
