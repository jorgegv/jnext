int f(int a)
{
    return a + 1;
}
#include "incmid.h"
int g(int a) { return f(a) * 2; }
int main(void)
{
    return g(3);
}
