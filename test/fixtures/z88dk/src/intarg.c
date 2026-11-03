unsigned int accum(unsigned int t, unsigned char v)
{
    unsigned int r;
    r = t + v * 3;
    return r;
}

int main(void)
{
    unsigned int x;
    x = accum(100, 7);
    x = accum(x, 2);
    return x;
}
