int g;
int f(int a)
{
    int t = a;
    if (a) {
        int p = t;
        t += p;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++)
            t += i + j;
    }
    return t;
}
int main(void) { g = f(1); return 0; }
