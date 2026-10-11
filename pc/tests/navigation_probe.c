#include <stdio.h>
int func_800E71DC(int, int);
static int heights[16];
int func_8008DC7C(int x, int z) { return heights[(x / 32) * 4 + z / 32]; }
int main(void)
{
    for (int i = 0; i < 16; ++i)
        heights[i] = i;
    if (func_800E71DC(0, 0) != 15)
        return 1;
    for (int i = 0; i < 16; ++i)
        heights[i] = (int)(0x80000000u + i);
    if (func_800E71DC(0, 0) != heights[10])
        return 1;
    heights[0] = 0;
    heights[1] = 1;
    heights[4] = 4;
    heights[5] = 5;
    if (func_800E71DC(0, 0) != heights[15])
        return 1;
    puts("PASS: navigation height selection, blocked center fallback, open 2x2 surface");
    return 0;
}
