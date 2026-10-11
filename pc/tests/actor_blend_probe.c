#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void func_800B147C(void*, void*, int, int, int);
static int half_calls, full_calls;
void func_800AE980(void* a, void* b, int n)
{
    (void)a;
    (void)b;
    if (n != 2)
        exit(1);
    ++half_calls;
}
void* vs_main_memcpy(void* a, const void* b, unsigned n)
{
    if (n != 0x60c)
        exit(1);
    ++full_calls;
    return memcpy(a, b, n);
}
void pc_unimplemented(const char* s)
{
    printf("unexpected trap: %s\n", s);
    exit(2);
}
#define CHECK(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} }while(0)
static unsigned dst[0x610 / 4], src[0x610 / 4];
int main(void)
{
    short* d = (short*)dst;
    short* s = (short*)src;
    /* Quarter blend across the 8192-unit angular wrap, signed translations,
     * and unsigned X scale. The padding starts nonzero to test word stores. */
    d[0] = 4000;
    s[0] = -4000;
    d[1] = -4000;
    s[1] = 4000;
    s[2] = -3;
    d[3] = 123;
    d[0x148 / 2] = -100;
    s[0x148 / 2] = 100;
    s[0x14c / 2] = 8;
    d[0x150 / 2] = (short)40000;
    s[0x150 / 2] = (short)40004;
    s[0x152 / 2] = -3;
    s[0x154 / 2] = 8;
    func_800B147C(dst, src, 1, 1, 4);
    CHECK(d[0] == 4048 && d[1] == -4048 && d[2] == -1 && d[3] == -1);
    CHECK(d[0x148 / 2] == -50 && d[0x14c / 2] == 2);
    CHECK(
        (unsigned short)d[0x150 / 2] == 40001 && d[0x152 / 2] == -1 && d[0x154 / 2] == 2);
    memset(dst, 0, sizeof(dst));
    memset(src, 0, sizeof(src));
    /* Root GPF leaves IR3=8. A zero Z rotation delta retains it, as on MIPS. */
    s[0x14c / 2] = 32;
    s[0] = 4;
    func_800B147C(dst, src, 1, 1, 4);
    CHECK(d[0] == 1 && d[2] == 2 && d[0x14c / 2] == 8);
    func_800B147C(dst, src, 2, 2, 4);
    CHECK(half_calls == 1 && !full_calls);
    src[0x608 / 4] = 0x12345678;
    dst[0x60c / 4] = 0xaabbccdd;
    func_800B147C(dst, src, 2, 4, 4);
    CHECK(
        full_calls == 1 && dst[0x608 / 4] == 0x12345678 && dst[0x60c / 4] == 0xaabbccdd);
    puts(
        "PASS: animation blend wrap, GTE rounding/state, signed fields and half/full fast paths");
    return 0;
}
