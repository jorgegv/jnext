long lsub(long a, long b)
{
    long d;
    d = a - b;
    return d;
}

int sw(unsigned int v)
{
    unsigned int r;
    switch (v) {
    case 100: r = 1; break;
    case 200: r = 2; break;
    case 300: r = 3; break;
    case 400: r = 4; break;
    case 500: r = 5; break;
    case 600: r = 6; break;
    default: r = 0; break;
    }
    return r;
}

int main(void)
{
    long x;
    int y;
    x = lsub(100000L, 1234L);
    y = sw(200);
    return y + (int)x;
}
