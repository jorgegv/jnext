#include <types.h>

static u8 u8val;
static u16 u16val;

void test_fastcall_u8(u8 u8arg) fastcall
{
    u8val = u8arg;
}

void test_fastcall_u16(u16 u16arg) fastcall
{
    u16val = u16arg;
}

void test_callee_u8(u8 u8arg) callee
{
    u8val = u8arg;
}

void test_callee_u16(u16 u16arg) callee
{
    u16val = u16arg;
}

void test_stdcall_u8(u8 u8arg) stdcall
{
    u8val = u8arg;
}

void test_stdcall_u16(u16 u16arg) stdcall
{
    u16val = u16arg;
}

void test_widechar_to_u16(void)
{
    test_fastcall_u16('R2');
}

void test_char_to_u16(void)
{
    test_fastcall_u16('R');
}