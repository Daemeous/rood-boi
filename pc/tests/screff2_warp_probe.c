#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
void pc_screff2_warp_packets(unsigned*, unsigned short, short, unsigned short*);
unsigned short D_80040C18[192] = { [0] = 4096, [64] = 5792 };
void pc_unimplemented(const char* s)
{
    fprintf(stderr, "unexpected trap: %s\n", s);
    exit(2);
}
#define XY(x, y) ((unsigned short)(x)|((unsigned)(unsigned short)(y)<<16))
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    unsigned* ot = mmap((void*)0x80000000, 0x20000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    CHECK(ot != MAP_FAILED);
    for (unsigned i = 1; i < 2048; ++i)
        ot[i] = ((unsigned)(ot + i - 1) & 0xffffff);
    ot[0] = 0xab123456;
    unsigned* p = ot + 0x10000 / 4;
    p[0] = 0x08000000 | ot[2046];
    ot[2046] = (unsigned)p & 0xffffff;
    p[1] = 0xe1000000;
    p[2] = 0x24808080;
    p[3] = XY(160, 112);
    p[4] = 0;
    p[5] = XY(168, 112);
    p[6] = 0;
    p[7] = XY(160, 120);
    p[8] = 0;
    unsigned short factors[444];
    pc_screff2_warp_packets(ot, 4096, 0, factors);
    CHECK(p[3] == XY(160, 112) && p[5] == XY(168, 112) && p[7] == XY(160, 120));
    CHECK(ot[0] == 0xab123456 && factors[0] == 8192 && factors[443] == 8192);
    pc_screff2_warp_packets(ot, 2048, 0, factors);
    CHECK(p[5] == XY(164, 112) && p[7] == XY(160, 116) && p[1] == 0xe1000000);
    /* Different radii must select different factors; negative coordinates
     * retain arithmetic-shift rounding. Distances 64 and 128 use known LUT slots. */
    p[3] = XY(168, 112);
    p[5] = XY(168, 120);
    p[7] = XY(152, 112);
    pc_screff2_warp_packets(ot, 4096, -256, factors);
    CHECK(factors[8] == 4888 && factors[11] == 4027);
    CHECK(p[3] == XY(164, 112) && p[5] == XY(163, 115) && p[7] == XY(155, 112));
    p[0] = 0x0c000000 | (p[0] & 0xffffff);
    p[1] = 0x3c808080;
    for (unsigned i = 0; i < 4; ++i) {
        p[2 + i * 3] = XY(168, 112);
        p[3 + i * 3] = 0x1234;
        if (i)
            p[1 + i * 3] = 0x112233;
    }
    pc_screff2_warp_packets(ot, 2048, 0, factors);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(p[2 + i * 3] == XY(164, 112) && p[3 + i * 3] == 0x1234);
    p[0] = 0x03000000 | (p[0] & 0xffffff);
    p[1] = 0x600000ff;
    p[2] = XY(168, 112);
    p[3] = XY(8, 8);
    pc_screff2_warp_packets(ot, 4096, -2048, factors);
    CHECK(p[2] == XY(168, 112) && factors[0] == 8192 && factors[1] == 4096
          && factors[2] == 2048 && ot[0] == 0xab123456);
    munmap(ot, 0x20000);
    puts(
        "PASS: screen warp identity/scale, FT3/GT4 strides, prefix commands, sprite exclusion, factor recurrence and OT restoration");
    return 0;
}
