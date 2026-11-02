#include "types.h"

int16_t g_int = -5;
uint16_t g_uint = 40000;
int8_t g_schar = -3;
char g_char = 'A';
int32_t g_long = 0x12345678;
uint32_t g_ulong = 0xCAFEBABE;
float g_float = 1.5;
char g_str[] = "hi";
const char *g_cptr = "abc";
uint8_t *g_bptr;
int16_t g_matrix[2][3] = { {1, 2, 3}, {4, 5, 6} };
struct point g_pt = { 1, 2, 3 };
struct point g_pts[3];
struct rect g_rect;
union word g_word;
node_t g_node;
node_t *g_head;
struct flags g_flags;
enum colour g_colour = BLUE;
byte_t g_byte = 7;
void (*g_fnptr)(void);
int16_t (*g_fnptr2)(int16_t);
static int16_t s_file = 9;
static uint8_t s_dup;

static int16_t sum(int16_t a, int16_t b, struct point *p, uint8_t k)
{
    int16_t t;
    int32_t acc;
    static uint8_t calls;
    calls++;
    t = a + b + k;
    acc = (int32_t)t * 3;
    if (p) {
        int16_t inner;
        inner = p->x + p->y;
        t += inner;
    }
    return t + (int16_t)acc;
}

int main(void)
{
    struct point lp;
    int16_t arr[4];
    uint8_t i;
    char c = 'x';
    int32_t big;
    struct point *pp;
    lp.x = 3; lp.y = 4; lp.tag = 5;
    pp = &lp;
    big = 100000;
    for (i = 0; i < 4; i++) {
        arr[i] = sum(i, g_int, pp, c);
    }
    g_head = &g_node;
    g_node.val = big;
    s_file = arr[3] + c;
    s_dup = (uint8_t)other(s_file);
    g_fnptr2 = other;
    while (1) {
        g_int++;
    }
    return 0;
}
